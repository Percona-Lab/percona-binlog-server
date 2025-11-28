// Copyright (c) 2023-2024 Percona and/or its affiliates.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 2.0,
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License, version 2.0, for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

#include "binsrv/gtids/gtid_set.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <boost/icl/concept/interval.hpp>
#include <boost/icl/concept/interval_set.hpp>

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid.hpp"
#include "binsrv/gtids/tag.hpp"
#include "binsrv/gtids/uuid.hpp"

#include "util/byte_span_extractors.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::gtids {

gtid_set::gtid_set() = default;
gtid_set::gtid_set(const gtid_set &other) = default;
gtid_set::gtid_set(gtid_set &&other) noexcept = default;
gtid_set &gtid_set::operator=(const gtid_set &other) = default;
gtid_set &gtid_set::operator=(gtid_set &&other) noexcept = default;
gtid_set::~gtid_set() = default;

gtid_set::gtid_set(util::const_byte_span portion) {
  // Gtid_set encoding:
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/sql/rpl_gtid_set.cc#L1389
  // Gtid_set decoding:
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/sql/rpl_gtid_set.cc#L1442

  auto remainder{portion};
  // Header (first 8 bytes) encoding:
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/sql/rpl_gtid_set.cc#L1353
  // Header (first 8 bytes) decoding:
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/sql/rpl_gtid_set.cc#L1365

  // In 8.0, where there is no tagged GTIDs, the header is 8-byte long and is
  // interpreted as a single 64-bit unsigned integer representing the number
  // of TSIDs (the (UUID + Tag) pairs).
  // <number_of_tsids>
  // 0<------------->7
  //
  // In 8.4, the same 8 bytes are interpreted differently:
  // <gtid_format> <number_of_tsids> <gtid_format>
  // 0<--------->0 1<------------->6 7<--------->7
  //
  // In other words, the highest byte (byte 7), which was always 0 in 8.0
  // (because the number of TSIDs was never >= 2^56), in 8.4 is interpreted
  // as a <gtid_format>:
  // '0' for untagged GTIDs,
  // '1' for tagged GTIDs.
  // MySQL developers also decided to duplicate this <gtid_format> also in
  // the very first byte (byte 0).
  // To sum up, the extraction rules are the following:
  // - if the highest byte is equal to '1', extract bytes 1..6 and put them
  // into a 64-bit unsigned integer,
  // - if it is '0', extract bytes 0..7 (all bytes) and put them into a
  // 64-bit unsigned integer.
  std::uint64_t header{};
  if (!util::extract_fixed_int_from_byte_span_checked(remainder, header)) {
    util::exception_location().raise<std::invalid_argument>(
        "encoded GTID set is too short to extract header");
  }

  // a helper lambda to parse encoded GTID set header (supports both
  // tagged and untagget encodings)
  const auto header_parser{[](std::uint64_t value) {
    static constexpr std::size_t format_bit_width{8U};
    static constexpr std::size_t format_bit_position{
        std::numeric_limits<decltype(value)>::digits - format_bit_width};

    const auto gtid_format_field{
        static_cast<std::uint8_t>(value >> format_bit_position)};
    // if gtid_format_field is anything but 0 or 1
    if ((gtid_format_field >> 1U) != 0U) {
      util::exception_location().raise<std::invalid_argument>(
          "invalid header in the encoded GTID set");
    }
    const auto tagged_flag{gtid_format_field == 1U};
    if (tagged_flag) {
      value <<= format_bit_width;
      value >>= (2U * format_bit_width);
    }
    if (value > std::numeric_limits<std::size_t>::max()) {
      util::exception_location().raise<std::invalid_argument>(
          "the number of TSIDs in the encoded GTID set is too large");
    }
    return std::pair(tagged_flag, static_cast<std::size_t>(value));
  }};

  // a helper lambda to parse encoded tag (for tagged encodings)
  const auto tag_parser{[](util::const_byte_span &source, tag_storage &target) {
    std::size_t extracted_tag_length{};
    if (!util::extract_varlen_int_from_byte_span_checked(
            source, extracted_tag_length)) {
      util::exception_location().raise<std::invalid_argument>(
          "encoded GTID set is too short to extract tag length");
    }

    target.resize(extracted_tag_length);
    const std::span<gtids::tag_storage::value_type> target_view{target};
    if (!util::extract_byte_span_from_byte_span_checked(source, target_view)) {
      util::exception_location().raise<std::invalid_argument>(
          "encoded GTID set is too short to extract tag data");
    }
  }};

  const auto [tagged, number_of_tsids]{header_parser(header)};
  uuid_storage current_uuid_raw{};
  tag_storage current_tag_raw{};
  for (std::size_t tsid_idx{0}; tsid_idx < number_of_tsids; ++tsid_idx) {
    if (!util::extract_byte_array_from_byte_span_checked(remainder,
                                                         current_uuid_raw)) {
      util::exception_location().raise<std::invalid_argument>(
          "encoded GTID set is too short to extract UUID");
    }
    const uuid current_uuid{current_uuid_raw};

    // extracting tag only if there was a tagged flag set in the header
    if (tagged) {
      tag_parser(remainder, current_tag_raw);
    }
    const tag current_tag{current_tag_raw};

    process_intervals(remainder, current_uuid, current_tag);
  }
}

[[nodiscard]] bool gtid_set::contains(const gtid &value) const noexcept {
  const auto uuid_it{data_.find(value.get_uuid())};
  if (uuid_it == std::cend(data_)) {
    return false;
  }

  const auto &tagged_gnos{uuid_it->second};
  const auto tag_it{tagged_gnos.find(value.get_tag())};
  if (tag_it == std::cend(tagged_gnos)) {
    return false;
  }

  const auto &gnos{tag_it->second};
  return boost::icl::contains(gnos, value.get_gno());
}

void gtid_set::add(const uuid &uuid_component, const tag &tag_component,
                   gno_t gno_component) {
  gtid::validate_components(uuid_component, tag_component, gno_component);
  data_[uuid_component][tag_component] += gno_component;
}

void gtid_set::add(const gtid &value) {
  if (value.is_empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot add an empty gtid");
  }
  data_[value.get_uuid()][value.get_tag()] += value.get_gno();
}
void gtid_set::add(const gtid_set &values) {
  for (const auto &[current_uuid, current_tagged_gnos] : values.data_) {
    for (const auto &[current_tag, current_gnos] : current_tagged_gnos) {
      data_[current_uuid][current_tag] += current_gnos;
    }
  }
}

void gtid_set::add_interval(const uuid &uuid_component,
                            const tag &tag_component, gno_t gno_lower_component,
                            gno_t gno_upper_component) {
  if (gno_upper_component == gno_lower_component) {
    add(uuid_component, tag_component, gno_lower_component);
    return;
  }
  if (gno_upper_component < gno_lower_component) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot add an interval with invalid bounds");
  }
  gtid::validate_components(uuid_component, tag_component, gno_lower_component);
  data_[uuid_component][tag_component] += gno_container::interval_type::closed(
      gno_lower_component, gno_upper_component);
}

void gtid_set::clear() noexcept { data_.clear(); }

bool operator==(const gtid_set &first,
                const gtid_set &second) noexcept = default;

void gtid_set::process_intervals(util::const_byte_span &remainder,
                                 const uuid &current_uuid,
                                 const tag &current_tag) {
  // a helper lambda to parse encoded interval
  const auto interval_parser{[](util::const_byte_span &source,
                                std::uint64_t &lower, std::uint64_t &upper) {
    // in this encoding we have a half-open interval
    // [current_interval_lower; current_interval_upper)
    if (!util::extract_fixed_int_from_byte_span_checked(source, lower)) {
      util::exception_location().raise<std::invalid_argument>(
          "encoded GTID set is too short to extract the lower component of "
          "the interval");
    }
    if (!util::extract_fixed_int_from_byte_span_checked(source, upper)) {
      util::exception_location().raise<std::invalid_argument>(
          "encoded GTID set is too short to extract the upper component of "
          "the interval");
    }
    if (upper <= lower) {
      util::exception_location().raise<std::invalid_argument>(
          "lower and upper components of the interval in the encoded GTID "
          "set do not represent a valid range ");
    }
  }};

  std::uint64_t current_number_of_intervals_raw{};
  if (!util::extract_fixed_int_from_byte_span_checked(
          remainder, current_number_of_intervals_raw)) {
    util::exception_location().raise<std::invalid_argument>(
        "encoded GTID set is too short to number of intervals");
  }
  if (current_number_of_intervals_raw >
      std::numeric_limits<std::size_t>::max()) {
    util::exception_location().raise<std::invalid_argument>(
        "the number of intervals in the encoded GTID set is too large");
  }
  const auto current_number_of_intervals{
      static_cast<std::size_t>(current_number_of_intervals_raw)};

  std::uint64_t current_interval_lower{};
  std::uint64_t current_interval_upper{};

  for (std::size_t interval_idx{0U}; interval_idx < current_number_of_intervals;
       ++interval_idx) {
    interval_parser(remainder, current_interval_lower, current_interval_upper);
    // TODO: validate that interval boundary values are increasing between
    // iterations

    // here we need to decrement upper bound as we have a halp-open interval
    // in the encoded representation and use closed interval in the
    // gtid_set::add_interval() method
    --current_interval_upper;

    add_interval(current_uuid, current_tag, current_interval_lower,
                 current_interval_upper);
  }
}

std::ostream &operator<<(std::ostream &output, const gtid_set &obj) {
  const auto gno_container_printer{
      [](std::ostream &stream, const gtid_set::gno_container &gnos) {
        for (const auto &interval : gnos) {
          const auto lower = boost::icl::lower(interval);
          const auto upper = boost::icl::upper(interval);
          stream << gtid::gno_separator << lower;
          if (upper != lower) {
            stream << gtid_set::interval_separator << upper;
          }
        }
      }};

  bool first_uuid{true};
  for (const auto &[current_uuid, current_tagged_gnos] : obj.data_) {
    if (!first_uuid) {
      output << gtid_set::uuid_separator << output.fill();
    } else {
      first_uuid = false;
    }
    output << current_uuid;
    for (const auto &[current_tag, current_gnos] : current_tagged_gnos) {
      if (!current_tag.is_empty()) {
        output << gtid::tag_separator << current_tag;
      }
      gno_container_printer(output, current_gnos);
    }
  }
  return output;
}

} // namespace binsrv::gtids
