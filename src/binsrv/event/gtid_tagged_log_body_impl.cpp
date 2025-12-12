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

#include "binsrv/event/gtid_tagged_log_body_impl.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <limits>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/lexical_cast.hpp>

#include <boost/align/align_up.hpp>

#include <boost/date_time/posix_time/conversion.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/posix_time/time_formatters_limited.hpp>

#include "binsrv/event/code_type.hpp"
#include "binsrv/event/gtid_log_flag_type.hpp"

#include "binsrv/gtids/uuid.hpp"

#include "util/bounded_string_storage.hpp"
#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/flag_set.hpp"
#include "util/semantic_version.hpp"

namespace binsrv::event {

generic_body_impl<code_type::gtid_tagged_log>::generic_body_impl(
    util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  // make sure we did OK with data members reordering
  static_assert(
      sizeof *this ==
          boost::alignment::align_up(
              sizeof flags_ + sizeof uuid_ + sizeof gno_ + sizeof tag_ +
                  sizeof last_committed_ + sizeof sequence_number_ +
                  sizeof immediate_commit_timestamp_ +
                  sizeof original_commit_timestamp_ +
                  sizeof transaction_length_ + sizeof original_server_version_ +
                  sizeof immediate_server_version_ +
                  sizeof commit_group_ticket_,
              alignof(decltype(*this))),
      "inefficient data member reordering in gtid_log event body");

  auto remainder = portion;
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/serialization/readme.md?plain=1#L102
  // <message_field> ::= <serialization_version_number> <message_format>
  // Extracting <serialization_version_number>

  static constexpr std::uint8_t expected_serialization_version_number{1U};
  std::uint8_t serialization_version_number{};
  if (!util::extract_varlen_int_from_byte_span_checked(
          remainder, serialization_version_number)) {
    util::exception_location().raise<std::invalid_argument>(
        "gtid_tagged_log event body is too short to extract "
        "serialization_version_number");
  }
  if (serialization_version_number != expected_serialization_version_number) {
    util::exception_location().raise<std::invalid_argument>(
        "unexpected serialization_version_number in the gtid_tagged_log event "
        "body");
  }

  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/serialization/readme.md?plain=1#L113
  // <message_format> ::= <serializable_field_size>
  // <last_non_ignorable_field_id> { <type_field> } Extracting
  // <serializable_field_size>
  std::size_t serializable_field_size{};
  if (!util::extract_varlen_int_from_byte_span_checked(
          remainder, serializable_field_size)) {
    util::exception_location().raise<std::invalid_argument>(
        "gtid_tagged_log event body is too short to extract "
        "serializable_field_size");
  }

  if (serializable_field_size != std::size(portion)) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid serializable_field_size in the gtid_tagged_log event body");
  }

  // Extracting <last_non_ignorable_field_id>
  std::uint8_t last_non_ignorable_field_id{};
  if (!util::extract_varlen_int_from_byte_span_checked(
          remainder, last_non_ignorable_field_id)) {
    util::exception_location().raise<std::invalid_argument>(
        "gtid_tagged_log event body is too short to extract "
        "last_non_ignorable_field_id");
  }

  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/serialization/readme.md?plain=1#L114
  // The sequence of <type_field>
  // <type_field> ::= <field_id> <field_data>
  std::uint8_t field_id{};
  std::uint8_t last_seen_field_id{std::numeric_limits<std::uint8_t>::max()};
  while (util::extract_varlen_int_from_byte_span_checked(remainder, field_id)) {
    if (last_seen_field_id != std::numeric_limits<std::uint8_t>::max() &&
        field_id <= last_seen_field_id) {
      util::exception_location().raise<std::invalid_argument>(
          "broken field_id sequence in the gtid_tagged_log event body");
    }
    if (field_id <= last_non_ignorable_field_id) {
      if (field_id != 0 && field_id != last_seen_field_id + 1U) {
        util::exception_location().raise<std::invalid_argument>(
            "violated last_non_ignorable_field_id rule in the gtid_tagged_log "
            "event body");
      }
    }
    process_field_data(field_id, remainder);
    last_seen_field_id = field_id;
  }

  if (!remainder.empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "extra bytes in the gtid_log event body");
  }
}

[[nodiscard]] gtid_log_flag_set
generic_body_impl<code_type::gtid_tagged_log>::get_flags() const noexcept {
  return gtid_log_flag_set{get_flags_raw()};
}

[[nodiscard]] std::string
generic_body_impl<code_type::gtid_tagged_log>::get_readable_flags() const {
  return to_string(get_flags());
}

[[nodiscard]] gtids::uuid
generic_body_impl<code_type::gtid_tagged_log>::get_uuid() const noexcept {
  return gtids::uuid{get_uuid_raw()};
}

[[nodiscard]] std::string
generic_body_impl<code_type::gtid_tagged_log>::get_readable_uuid() const {
  return boost::lexical_cast<std::string>(get_uuid());
}

[[nodiscard]] std::string_view
generic_body_impl<code_type::gtid_tagged_log>::get_tag() const noexcept {
  return util::to_string_view(get_tag_raw());
}

[[nodiscard]] std::string generic_body_impl<code_type::gtid_tagged_log>::
    get_readable_immediate_commit_timestamp() const {
  // threre is still no way to get string representationof the
  // std::chrono::high_resolution_clock::time_point using standard stdlib means,
  // so using boost::posix_time::ptime here
  boost::posix_time::ptime timestamp{
      boost::posix_time::from_time_t(std::time_t{})};
  timestamp +=
      boost::posix_time::microseconds{get_immediate_commit_timestamp_raw()};
  return boost::posix_time::to_iso_extended_string(timestamp);
}

[[nodiscard]] util::semantic_version
generic_body_impl<code_type::gtid_tagged_log>::get_original_server_version()
    const noexcept {
  return util::semantic_version{get_original_server_version_raw()};
}

[[nodiscard]] std::string generic_body_impl<
    code_type::gtid_tagged_log>::get_readable_original_server_version() const {
  return get_original_server_version().get_string();
}

[[nodiscard]] util::semantic_version
generic_body_impl<code_type::gtid_tagged_log>::get_immediate_server_version()
    const noexcept {
  return util::semantic_version{get_immediate_server_version_raw()};
}

[[nodiscard]] std::string generic_body_impl<
    code_type::gtid_tagged_log>::get_readable_immediate_server_version() const {
  return get_immediate_server_version().get_string();
}

std::ostream &
operator<<(std::ostream &output,
           const generic_body_impl<code_type::gtid_tagged_log> &obj) {
  output << "flags: " << obj.get_readable_flags()
         << ", uuid: " << obj.get_readable_uuid()
         << ", gno: " << obj.get_gno_raw() << ", tag: " << obj.get_tag()
         << ", last_committed: " << obj.get_last_committed_raw()
         << ", sequence_number: " << obj.get_sequence_number_raw();

  if (obj.has_immediate_commit_timestamp()) {
    output << ", immediate_commit_timestamp: "
           << obj.get_readable_immediate_commit_timestamp();
  }
  if (obj.has_original_commit_timestamp()) {
    output << ", original_commit_timestamp: "
           << obj.get_original_commit_timestamp_raw();
  }
  if (obj.has_transaction_length()) {
    output << ", transaction_length: " << obj.get_transaction_length_raw();
  }
  if (obj.has_original_server_version()) {
    output << ", original_server_version: "
           << obj.get_readable_original_server_version();
  }
  if (obj.has_immediate_server_version()) {
    output << ", immediate_server_version: "
           << obj.get_readable_immediate_server_version();
  }
  if (obj.has_commit_group_ticket()) {
    output << ", commit_group_ticket: " << obj.get_commit_group_ticket_raw();
  }

  return output;
}

void generic_body_impl<code_type::gtid_tagged_log>::process_field_data(
    std::uint8_t field_id, util::const_byte_span &remainder) {
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.h#L1111
  enum class field_id_type : std::uint8_t {
    flags,
    uuid,
    gno,
    tag,
    last_committed,
    sequence_number,
    immediate_commit_timestamp,
    original_commit_timestamp,
    transaction_length,
    immediate_server_version,
    original_server_version,
    commit_group_ticket,

    delimiter
  };

  const auto varlen_int_extractor{
      [](util::const_byte_span &source, auto &target, std::string_view label) {
        if (!util::extract_varlen_int_from_byte_span_checked(source, target)) {
          util::exception_location().raise<std::invalid_argument>(
              "gtid_tagged_log event body is too short to extract " +
              std::string{label});
        }
      }};

  switch (static_cast<field_id_type>(field_id)) {
  case field_id_type::flags:
    varlen_int_extractor(remainder, flags_, "flags");
    break;
  case field_id_type::uuid: {
    // Extracting a fixed-size (16 byte) array of varlen bytes
    std::uint8_t extracted_uuid_byte{};
    for (auto &uuid_byte : uuid_) {
      if (!util::extract_varlen_int_from_byte_span_checked(
              remainder, extracted_uuid_byte)) {
        util::exception_location().raise<std::invalid_argument>(
            "gtid_tagged_log event body is too short to extract uuid");
      }
      uuid_byte = static_cast<std::byte>(extracted_uuid_byte);
    }
  } break;
  case field_id_type::gno:
    // TODO: add validation (gno >= gtid::min_gno && gno_ < gtid::max_gno)
    varlen_int_extractor(remainder, gno_, "gno");
    break;
  case field_id_type::tag: {
    // Extracting tag (length encoded as varlent int and raw character array)
    std::size_t extracted_tag_length{};
    varlen_int_extractor(remainder, extracted_tag_length, "tag length");
    tag_.resize(extracted_tag_length);
    const std::span<gtids::tag_storage::value_type> tag_subrange{tag_};
    if (!util::extract_byte_span_from_byte_span_checked(remainder,
                                                        tag_subrange)) {
      util::exception_location().raise<std::invalid_argument>(
          "gtid_tagged_log event body is too short to extract tag data");
    }
  } break;
  case field_id_type::last_committed:
    varlen_int_extractor(remainder, last_committed_, "last_committed");
    break;
  case field_id_type::sequence_number:
    varlen_int_extractor(remainder, sequence_number_, "sequence_number");
    break;
  case field_id_type::immediate_commit_timestamp:
    varlen_int_extractor(remainder, immediate_commit_timestamp_,
                         "immediate_commit_timestamp");
    break;
  case field_id_type::original_commit_timestamp:
    varlen_int_extractor(remainder, original_commit_timestamp_,
                         "original_commit_timestamp");
    break;
  case field_id_type::transaction_length:
    varlen_int_extractor(remainder, transaction_length_, "transaction_length");
    break;
  case field_id_type::immediate_server_version:
    varlen_int_extractor(remainder, immediate_server_version_,
                         "immediate_server_version");
    break;
  case field_id_type::original_server_version:
    varlen_int_extractor(remainder, original_server_version_,
                         "original_server_version");
    break;
  case field_id_type::commit_group_ticket:
    varlen_int_extractor(remainder, commit_group_ticket_,
                         "commit_group_ticket");
    break;
  default:
    util::exception_location().raise<std::invalid_argument>(
        "unknown field_id in the gtid_tagged_log event body");
  }
}

} // namespace binsrv::event
