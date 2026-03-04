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

#include <array>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <ios>
#include <istream>
#include <iterator>
#include <limits>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <boost/icl/concept/interval.hpp>
#include <boost/icl/concept/interval_associator.hpp>
#include <boost/icl/concept/interval_set.hpp>

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid.hpp"
#include "binsrv/gtids/tag.hpp"
#include "binsrv/gtids/uuid.hpp"

#include "util/bnf_parser_helpers.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/byte_span_inserters.hpp"
#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::gtids {

class gtid_set_parser {
public:
  gtid_set_parser(const gtid_set_parser &) = delete;
  gtid_set_parser(gtid_set_parser &&) = delete;
  gtid_set_parser &operator=(const gtid_set_parser &) = delete;
  gtid_set_parser &operator=(gtid_set_parser &&) = delete;
  ~gtid_set_parser() = default;

  static void parse(std::string_view readable_value, gtid_set &gtids) {
    gtid_set_parser parser{gtids};
    std::string_view remainder{readable_value};
    parser.parse_gtid_set(remainder);
  }

private:
  /*
    BNF grammar for GTID sets

    <gtid_set> ::= <uuid_set> (',' <space> <uuid_set>)*
    <uuid_set> ::= <uuid> ':' <optionally_tagged_intervals>
    <uuid> ::= <hexadecimal_pair>{4} '-' <hexadecimal_pair>{2} '-'
               <hexadecimal_pair>{2} '-' <hexadecimal_pair>{2} '-'
               <hexadecimal_pair>{6}
    <hexadecimal_pair> ::= <hexadecimal>{2}
    <hexadecimal> ::= [a-fA-F0-9]
    <optionally_tagged_intervals> ::= <optionally_tagged_interval>
                                      (':' <optionally_tagged_interval>)*
    <optionally_tagged_interval> ::= (<tag> ':')? <gno> ('-' <gno>)?
    <tag> ::= [a-zA-Z_][a-zA-Z0-9_]{0,31}
    <gno> ::= [0-9]+
    <space> ::= [ \t\r\n\f\v]+
  */

  gtid_set *result_gtids_;
  uuid current_uuid_{};
  tag current_tag_{};

  explicit gtid_set_parser(gtid_set &gtids) noexcept : result_gtids_(&gtids) {}

  static constexpr auto hexadecimal_predicate{
      [](char character) noexcept -> bool {
        return std::isxdigit(character) != 0;
      }};
  static constexpr auto tag_first_character_predicate{
      [](char character) noexcept -> bool {
        return character == '_' || std::isalpha(character) != 0;
      }};
  static constexpr auto tag_other_characters_predicate{
      [](char character) noexcept -> bool {
        return character == '_' || std::isalnum(character) != 0;
      }};
  static constexpr auto digit_predicate{[](char character) noexcept -> bool {
    return std::isdigit(character) != 0;
  }};
  static constexpr auto space_predicate{[](char character) noexcept -> bool {
    return std::isspace(character) != 0;
  }};

  template <typename T>
  [[nodiscard]] static T char_to_value(char character, char base,
                                       T shift = T{}) noexcept {
    return static_cast<T>(static_cast<T>(util::to_unsigned(character - base)) +
                          shift);
  }

  // <hexadecimal> ::= [a-fA-F0-9]
  [[nodiscard]] static std::uint8_t
  parse_hexadecimal(std::string_view &remainder) {
    static constexpr char lower_a_character{'a'};
    static constexpr char upper_a_character{'A'};
    static constexpr char zero_digit_character{'0'};

    static constexpr std::uint8_t hex_shift{10U};

    const char character{
        util::parse_character_predicate(remainder, hexadecimal_predicate)};
    if (character >= lower_a_character) {
      return char_to_value<std::uint8_t>(character, lower_a_character,
                                         hex_shift);
    }
    if (character >= upper_a_character) {
      return char_to_value<std::uint8_t>(character, upper_a_character,
                                         hex_shift);
    }
    return char_to_value<std::uint8_t>(character, zero_digit_character);
  }

  // <hexadecimal_pair> ::= <hexadecimal>{2}
  [[nodiscard]] static std::byte
  parse_hexadecimal_pair(std::string_view &remainder) {
    std::uint8_t result{parse_hexadecimal(remainder)};
    result <<= 4U;
    result |= parse_hexadecimal(remainder);
    return util::from_underlying<std::byte>(result);
  }

  // <uuid> ::= <hexadecimal_pair>{4} '-' <hexadecimal_pair>{2} '-'
  //            <hexadecimal_pair>{2} '-' <hexadecimal_pair>{2} '-'
  //            <hexadecimal_pair>{6}
  [[nodiscard]] static uuid parse_uuid(std::string_view &remainder) {
    uuid_storage buffer{};
    auto *buffer_it{std::begin(buffer)};

    const auto parse_group{
        [](std::string_view &input, std::byte *&output, std::size_t count) {
          for (std::size_t index{0U}; index < count; ++index) {
            *output = parse_hexadecimal_pair(input);
            std::advance(output, 1U);
          }
        }};
    static constexpr std::array group_sizes{4U, 2U, 2U, 2U, 6U};
    const auto *group_it{std::cbegin(group_sizes)};
    const auto *const group_en{std::cend(group_sizes)};

    parse_group(remainder, buffer_it, *group_it);
    std::advance(group_it, 1U);

    while (group_it != group_en) {
      util::parse_character(remainder, uuid::group_separator);
      parse_group(remainder, buffer_it, *group_it);
      std::advance(group_it, 1U);
    }

    assert(buffer_it == std::end(buffer));
    return uuid{buffer};
  }

  // <tag> ::= [a-zA-Z_][a-zA-Z0-9_]{0,31}
  [[nodiscard]] static tag parse_tag(std::string_view &remainder) {
    tag_storage buffer{};
    char tag_character{util::parse_character_predicate(
        remainder, tag_first_character_predicate)};
    buffer.push_back(
        util::from_underlying<std::byte>(util::to_unsigned(tag_character)));

    while (std::size(buffer) < tag_max_length &&
           util::parse_character_predicate_ex(
               remainder, tag_other_characters_predicate, tag_character)) {
      buffer.push_back(
          util::from_underlying<std::byte>(util::to_unsigned(tag_character)));
    }
    assert(std::size(buffer) <= tag_max_length);

    return tag{buffer};
  }

  // <gno> ::= [0-9]+
  [[nodiscard]] static gno_t parse_gno(std::string_view &remainder) {
    static constexpr gno_t radix{10ULL};
    gno_t result{0ULL};

    char digit{};

    digit = util::parse_character_predicate(remainder, digit_predicate);
    result += char_to_value<gno_t>(digit, '0', 0U);

    while (
        util::parse_character_predicate_ex(remainder, digit_predicate, digit)) {
      // TODO: add overflow checks
      result *= radix;
      result += char_to_value<gno_t>(digit, '0', 0U);
    }
    return result;
  }

  // <optionally_tagged_interval> ::= (<tag> ':')? <gno> ('-' <gno>)?
  void parse_optionally_tagged_interval(std::string_view &remainder) {
    if (!util::check_character_predicate(remainder, digit_predicate)) {
      current_tag_ = parse_tag(remainder);
      util::parse_character(remainder, gtid::component_separator);
    }
    const gno_t gno_lower{parse_gno(remainder)};
    char character{};
    if (util::parse_character_ex(remainder, gtid_set::interval_separator,
                                 character)) {
      const gno_t gno_upper{parse_gno(remainder)};
      result_gtids_->add_interval(current_uuid_, current_tag_, gno_lower,
                                  gno_upper);
    } else {
      result_gtids_->add(current_uuid_, current_tag_, gno_lower);
    }
  }

  // <optionally_tagged_intervals> ::= <optionally_tagged_interval>
  //                                   (':' <optionally_tagged_interval>)*
  void parse_optionally_tagged_intervals(std::string_view &remainder) {
    parse_optionally_tagged_interval(remainder);
    char character{};
    while (util::parse_character_ex(remainder, gtid::component_separator,
                                    character)) {
      parse_optionally_tagged_interval(remainder);
    }
  }

  // <uuid_set> ::= <uuid> ':' <optionally_tagged_intervals>
  void parse_uuid_set(std::string_view &remainder) {
    current_uuid_ = parse_uuid(remainder);
    current_tag_ = tag{};
    util::parse_character(remainder, gtid::component_separator);
    parse_optionally_tagged_intervals(remainder);
  }

  // <space> ::= [ \t\r\n\f\v]+
  static void parse_space(std::string_view &remainder) {
    util::parse_character_predicate(remainder, space_predicate);

    char character{};
    while (util::parse_character_predicate_ex(remainder, space_predicate,
                                              character)) {
    }
  }

  // <gtid_set> ::= <uuid_set> (',' <space> <uuid_set>)*
  void parse_gtid_set(std::string_view &remainder) {
    parse_uuid_set(remainder);
    char character{};
    while (util::parse_character_ex(remainder, gtid_set::uuid_separator,
                                    character)) {
      parse_space(remainder);
      parse_uuid_set(remainder);
    }
  }
};

gtid_set::gtid_set() = default;
gtid_set::gtid_set(const gtid_set &other) = default;
gtid_set::gtid_set(gtid_set &&other) noexcept = default;
gtid_set &gtid_set::operator=(const gtid_set &other) = default;
gtid_set &gtid_set::operator=(gtid_set &&other) noexcept = default;
gtid_set::~gtid_set() = default;

gtid_set::gtid_set(std::string_view value) {
  // empty string correspond to an empty GTID set
  if (value.empty()) {
    return;
  }

  try {
    gtid_set_parser::parse(value, *this);
  } catch (const std::exception &) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot parse GTID set");
  }
}

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
  // MySQL developers also decided to duplicate this <gtid_format> in
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

    const auto gtid_format_field{static_cast<std::uint8_t>(
        value >>
        (std::numeric_limits<std::uint64_t>::digits - format_bit_width))};
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

  if (!remainder.empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "extra bytes in the encoded gtid_set");
  }
}

[[nodiscard]] bool gtid_set::contains_tags() const noexcept {
  for (const auto &[current_uuid, current_tagged_gnos] : data_) {
    for (const auto &[current_tag, current_gnos] : current_tagged_gnos) {
      if (!current_tag.is_empty()) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] std::string gtid_set::str() const {
  const auto string_size_estimator{
      [](const tagged_gnos_by_uid_container &data) -> std::size_t {
        // Rough estimate for up to 1 billion transactions
        static constexpr std::size_t average_gno_readable_size{9};
        std::size_t estimate{0U};
        for (const auto &[current_uuid, current_tagged_gnos] : data) {
          estimate += uuid::readable_size;
          estimate += 2; // for ", "

          for (const auto &[current_tag, current_gnos] : current_tagged_gnos) {
            ++estimate; // for ':' before tag
            estimate += current_tag.get_size();
            // for each interval one ':', one '-' and two numbers
            estimate += boost::icl::interval_count(current_gnos) *
                        (2U * average_gno_readable_size + 2U);
          }
        }
        return estimate;
      }};

  const auto gno_container_printer{
      [](std::string &result, const gno_container &gnos) {
        for (const auto &interval : gnos) {
          const auto lower = boost::icl::lower(interval);
          // we have a half-open interval in boost::icl::interval_set
          const auto upper = boost::icl::upper(interval) - 1ULL;
          result += gtid::component_separator;
          result += std::to_string(lower);
          if (upper != lower) {
            result += interval_separator;
            result += std::to_string(upper);
          }
        }
      }};

  std::string result{};
  result.reserve(string_size_estimator(data_));
  bool first_uuid{true};
  for (const auto &[current_uuid, current_tagged_gnos] : data_) {
    if (!first_uuid) {
      result += uuid_separator;
      result += uuid_separator_whitespace;
    } else {
      first_uuid = false;
    }
    result += current_uuid.str();

    for (const auto &[current_tag, current_gnos] : current_tagged_gnos) {
      if (!current_tag.is_empty()) {
        result += gtid::component_separator;
        result += current_tag.get_name();
      }
      gno_container_printer(result, current_gnos);
    }
  }
  return result;
}

[[nodiscard]] std::size_t gtid_set::calculate_encoded_size() const noexcept {
  const auto tagged_flag{contains_tags()};
  // 8 bytes for the header (for both tahgged and untagged versions)
  std::size_t result{sizeof(std::uint64_t)};
  for (const auto &[current_uuid, current_tagged_gnos] : data_) {
    for (const auto &[current_tag, current_gnos] : current_tagged_gnos) {
      // 16 bytes for UUID
      result += uuid::calculate_encoded_size();
      if (tagged_flag) {
        result += current_tag.calculate_encoded_size();
      }
      // 8 bytes for the number of intervals
      result += sizeof(std::uint64_t);
      // 16 bytes for each interval
      result +=
          boost::icl::interval_count(current_gnos) * 2U * sizeof(std::uint64_t);
    }
  }
  return result;
}

void gtid_set::encode_to(util::byte_span &destination) const {
  const auto tagged_flag{contains_tags()};

  util::byte_span remainder{destination};
  // skipping 8 bytes for the encoded header (number of tsids + tagged flag)
  remainder = remainder.subspan(sizeof(std::uint64_t));

  // a helper lambda to form encoded GTID set header (supports both
  // tagged and untagget encodings)
  const auto header_encoder{[](bool tagged, std::size_t number_of_tsids) {
    static constexpr std::size_t format_bit_width{8U};
    if (!tagged) {
      // ensuring that the value is less then 2^56
      if (number_of_tsids >=
          (1ULL << (std::numeric_limits<std::uint64_t>::digits -
                    format_bit_width))) {
        util::exception_location().raise<std::invalid_argument>(
            "the number of TSIDs in the untagged GTID set being encoded is too "
            "large");
      }
      return std::uint64_t{number_of_tsids};
    }

    // ensuring that the value is less then 2^48
    if (number_of_tsids >=
        (1ULL << (std::numeric_limits<std::uint64_t>::digits -
                  2U * format_bit_width))) {
      util::exception_location().raise<std::invalid_argument>(
          "the number of TSIDs in the tagged GTID set being encoded is too "
          "large");
    }
    // shifting 1 to 48 bits
    std::uint64_t result{1ULL << (std::numeric_limits<std::uint64_t>::digits -
                                  2U * format_bit_width)};
    result |= std::uint64_t{number_of_tsids};
    result <<= format_bit_width;
    result |= 1ULL;
    return result;
  }};

  std::size_t number_of_tsids{0ULL};
  for (const auto &[current_uuid, current_tagged_gnos] : data_) {
    for (const auto &[current_tag, current_gnos] : current_tagged_gnos) {
      // 16 bytes for UUID
      current_uuid.encode_to(remainder);
      if (tagged_flag) {
        // varlen bytes for tag size
        // 1 byte for each character in the tag
        current_tag.encode_to(remainder);
      }
      // 8 bytes for the number of intervals
      util::insert_fixed_int_to_byte_span(
          remainder, std::uint64_t{boost::icl::interval_count(current_gnos)});
      for (const auto &interval : current_gnos) {
        // 16 bytes for each interval
        util::insert_fixed_int_to_byte_span(
            remainder, std::uint64_t{boost::icl::lower(interval)});
        // here we do not need to do anything with the upper bound as we
        // already have a half-open interval in boost::icl::interval_set
        util::insert_fixed_int_to_byte_span(
            remainder, std::uint64_t{boost::icl::upper(interval)});
      }
      ++number_of_tsids;
    }
  }

  // writing header
  util::byte_span header{destination};
  util::insert_fixed_int_to_byte_span(
      header, header_encoder(tagged_flag, number_of_tsids));

  destination = remainder;
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
  data_[uuid_component][tag_component].insert(
      gno_container::interval_type::right_open(gno_component,
                                               gno_component + 1ULL));
}

void gtid_set::add(const gtid &value) {
  if (value.is_empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot add an empty gtid");
  }
  data_[value.get_uuid()][value.get_tag()].insert(
      gno_container::interval_type::right_open(value.get_gno(),
                                               value.get_gno() + 1ULL));
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
  data_[uuid_component][tag_component].insert(
      gno_container::interval_type::right_open(gno_lower_component,
                                               gno_upper_component + 1ULL));
}

void gtid_set::subtract(const uuid &uuid_component, const tag &tag_component,
                        gno_t gno_component) {
  gtid::validate_components(uuid_component, tag_component, gno_component);
  tagged_gnos_by_uid_container::iterator uuid_iterator{};
  gnos_by_tag_container::iterator tag_iterator{};
  if (!find_gnos(uuid_component, tag_component, uuid_iterator, tag_iterator)) {
    return;
  }
  tag_iterator->second.erase(gno_container::interval_type::right_open(
      gno_component, gno_component + 1ULL));
  cleanup_if_empty(uuid_iterator, tag_iterator);
}

void gtid_set::subtract(const gtid &value) {
  if (value.is_empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot subtract an empty gtid");
  }
  tagged_gnos_by_uid_container::iterator uuid_iterator{};
  gnos_by_tag_container::iterator tag_iterator{};
  if (!find_gnos(value.get_uuid(), value.get_tag(), uuid_iterator,
                 tag_iterator)) {
    return;
  }
  tag_iterator->second.erase(gno_container::interval_type::right_open(
      value.get_gno(), value.get_gno() + 1ULL));
  cleanup_if_empty(uuid_iterator, tag_iterator);
}

void gtid_set::subtract(const gtid_set &values) {
  for (const auto &[current_uuid, current_tagged_gnos] : values.data_) {
    for (const auto &[current_tag, current_gnos] : current_tagged_gnos) {
      tagged_gnos_by_uid_container::iterator uuid_iterator{};
      gnos_by_tag_container::iterator tag_iterator{};
      if (!find_gnos(current_uuid, current_tag, uuid_iterator, tag_iterator)) {
        continue;
      }
      tag_iterator->second -= current_gnos;
      cleanup_if_empty(uuid_iterator, tag_iterator);
    }
  }
}

void gtid_set::subtract_interval(const uuid &uuid_component,
                                 const tag &tag_component,
                                 gno_t gno_lower_component,
                                 gno_t gno_upper_component) {
  if (gno_upper_component == gno_lower_component) {
    subtract(uuid_component, tag_component, gno_lower_component);
    return;
  }
  if (gno_upper_component < gno_lower_component) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot subtract an interval with invalid bounds");
  }
  gtid::validate_components(uuid_component, tag_component, gno_lower_component);

  tagged_gnos_by_uid_container::iterator uuid_iterator{};
  gnos_by_tag_container::iterator tag_iterator{};
  if (!find_gnos(uuid_component, tag_component, uuid_iterator, tag_iterator)) {
    return;
  }

  tag_iterator->second.erase(gno_container::interval_type::right_open(
      gno_lower_component, gno_upper_component + 1ULL));
  cleanup_if_empty(uuid_iterator, tag_iterator);
}

void gtid_set::clear() noexcept { data_.clear(); }

bool operator==(const gtid_set &first,
                const gtid_set &second) noexcept = default;

bool intersects(const gtid_set &first, const gtid_set &second) noexcept {
  for (const auto &[current_uuid, current_tagged_gnos] : second.data_) {
    const auto uuid_iterator{first.data_.find(current_uuid)};
    if (uuid_iterator == std::cend(first.data_)) {
      continue;
    }
    for (const auto &[current_tag, current_gnos] : current_tagged_gnos) {
      const auto tag_iterator{uuid_iterator->second.find(current_tag)};
      if (tag_iterator == std::cend(uuid_iterator->second)) {
        continue;
      }
      if (boost::icl::intersects(tag_iterator->second, current_gnos)) {
        return true;
      }
    }
  }
  return false;
}

bool gtid_set::find_gnos(
    const uuid &uuid_component, const tag &tag_component,
    tagged_gnos_by_uid_container::iterator &uuid_iterator,
    gnos_by_tag_container::iterator &tag_iterator) noexcept {
  auto local_uuid_iterator{data_.find(uuid_component)};
  if (local_uuid_iterator == std::end(data_)) {
    return false;
  }
  auto local_tag_iterator{local_uuid_iterator->second.find(tag_component)};
  if (local_tag_iterator == std::end(local_uuid_iterator->second)) {
    return false;
  }
  uuid_iterator = local_uuid_iterator;
  tag_iterator = local_tag_iterator;
  return true;
}

void gtid_set::cleanup_if_empty(
    tagged_gnos_by_uid_container::iterator uuid_iterator,
    gnos_by_tag_container::iterator tag_iterator) noexcept {
  if (!tag_iterator->second.empty()) {
    return;
  }
  uuid_iterator->second.erase(tag_iterator);
  if (!uuid_iterator->second.empty()) {
    return;
  }
  data_.erase(uuid_iterator);
}

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

  gno_container &gnos{data_[current_uuid][current_tag]};
  for (std::size_t interval_idx{0U}; interval_idx < current_number_of_intervals;
       ++interval_idx) {
    interval_parser(remainder, current_interval_lower, current_interval_upper);
    // TODO: validate that interval boundary values are increasing between
    // iterations

    // here we do not need to do anything with the upper bound as we already
    // have a half-open interval in the encoded representation
    gnos.insert(gno_container::interval_type::right_open(
        current_interval_lower, current_interval_upper));
  }
}

std::ostream &operator<<(std::ostream &output, const gtid_set &obj) {
  return output << obj.str();
}

std::istream &operator>>(std::istream &input, gtid_set &obj) {
  std::string gtids_str;
  if (input.peek() != std::istream::traits_type::eof()) {
    std::getline(input, gtids_str);
  }
  if (!input) {
    return input;
  }
  try {
    obj = gtid_set{gtids_str};
  } catch (const std::exception &) {
    input.setstate(std::ios_base::failbit);
  }
  return input;
}

} // namespace binsrv::gtids
