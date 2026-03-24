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

#include "binsrv/events/common_header_view.hpp"

#include <cstdint>
#include <ctime>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_header_flag_type.hpp"

#include "util/byte_span_extractors.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/byte_span_inserters.hpp"
#include "util/conversion_helpers.hpp"
#include "util/ctime_timestamp.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/flag_set.hpp"

namespace binsrv::events {

[[nodiscard]] util::ctime_timestamp
common_header_view_base::get_timestamp_from_raw(
    std::uint32_t timestamp) noexcept {
  return util::ctime_timestamp{static_cast<std::time_t>(timestamp)};
}

[[nodiscard]] std::string
common_header_view_base::get_readable_timestamp_from_raw(
    std::uint32_t timestamp) {
  return get_timestamp_from_raw(timestamp).iso_extended_str();
}

[[nodiscard]] code_type common_header_view_base::get_type_code_from_raw(
    std::uint8_t type_code) noexcept {
  return static_cast<code_type>(type_code);
}

[[nodiscard]] std::string_view
common_header_view_base::get_readable_type_code_from_raw(
    std::uint8_t type_code) noexcept {
  return to_string_view(get_type_code_from_raw(type_code));
}

[[nodiscard]] common_header_flag_set
common_header_view_base::get_flags_from_raw(std::uint16_t flags) noexcept {
  return common_header_flag_set{flags};
}

[[nodiscard]] std::string
common_header_view_base::get_readable_flags_from_raw(std::uint16_t flags) {
  return to_string(get_flags_from_raw(flags));
}

common_header_view_base::common_header_view_base(util::byte_span portion)
    : portion_{portion} {
  /*
    https://github.com/mysql/mysql-server/blob/mysql-8.0.43/libbinlogevents/src/binlog_event.cpp#L198
    https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/binlog_event.cpp#L242

    The first 19 bytes in the header is as follows:
      +============================================+
      | member_variable               offset : len |
      +============================================+
      | when.tv_sec                        0 : 4   |
      +--------------------------------------------+
      | type_code       EVENT_TYPE_OFFSET(4) : 1   |
      +--------------------------------------------+
      | server_id       SERVER_ID_OFFSET(5)  : 4   |
      +--------------------------------------------+
      | data_written    EVENT_LEN_OFFSET(9)  : 4   |
      +--------------------------------------------+
      | log_pos           LOG_POS_OFFSET(13) : 4   |
      +--------------------------------------------+
      | flags               FLAGS_OFFSET(17) : 2   |
      +--------------------------------------------+
      | extra_headers                     19 : x-19|
      +============================================+
   */

  if (std::size(portion) != size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid event common header length");
  }

  if (get_type_code_raw() >= util::enum_to_index(code_type::delimiter) ||
      to_string_view(get_type_code()).empty()) {
    util::exception_location().raise<std::logic_error>(
        "invalid event code in event header");
  }
  // TODO: check if flags are valid (all the bits have corresponding enum)
}

[[nodiscard]] std::uint32_t
common_header_view_base::get_timestamp_raw() const noexcept {
  util::const_byte_span remainder{
      portion_.subspan(timestamp_offset, sizeof(std::uint32_t))};
  std::uint32_t timestamp{};
  util::extract_fixed_int_from_byte_span(remainder, timestamp);
  return timestamp;
}
[[nodiscard]] util::ctime_timestamp
common_header_view_base::get_timestamp() const noexcept {
  return get_timestamp_from_raw(get_timestamp_raw());
}

void common_header_view_base::set_timestamp_raw(
    std::uint32_t timestamp) const noexcept {
  util::byte_span remainder{
      portion_.subspan(timestamp_offset, sizeof timestamp)};
  util::insert_fixed_int_to_byte_span(remainder, timestamp);
}

[[nodiscard]] std::uint8_t
common_header_view_base::get_type_code_raw() const noexcept {
  util::const_byte_span remainder{
      portion_.subspan(type_code_offset, sizeof(std::uint8_t))};
  std::uint8_t type_code{};
  util::extract_fixed_int_from_byte_span(remainder, type_code);
  return type_code;
}
void common_header_view_base::set_type_code_raw(
    std::uint8_t type_code) const noexcept {
  util::byte_span remainder{
      portion_.subspan(type_code_offset, sizeof type_code)};
  util::insert_fixed_int_to_byte_span(remainder, type_code);
}

[[nodiscard]] std::uint32_t
common_header_view_base::get_server_id_raw() const noexcept {
  util::const_byte_span remainder{
      portion_.subspan(server_id_offset, sizeof(std::uint32_t))};
  std::uint32_t server_id{};
  util::extract_fixed_int_from_byte_span(remainder, server_id);
  return server_id;
}

void common_header_view_base::set_server_id_raw(
    std::uint32_t server_id) const noexcept {
  util::byte_span remainder{
      portion_.subspan(server_id_offset, sizeof server_id)};
  util::insert_fixed_int_to_byte_span(remainder, server_id);
}

[[nodiscard]] std::uint32_t
common_header_view_base::get_event_size_raw() const noexcept {
  util::const_byte_span remainder{
      portion_.subspan(event_size_offset, sizeof(std::uint32_t))};
  std::uint32_t event_size{};
  util::extract_fixed_int_from_byte_span(remainder, event_size);
  return event_size;
}
void common_header_view_base::set_event_size_raw(
    std::uint32_t event_size) const noexcept {
  util::byte_span remainder{
      portion_.subspan(event_size_offset, sizeof event_size)};
  util::insert_fixed_int_to_byte_span(remainder, event_size);
}

[[nodiscard]] std::uint32_t
common_header_view_base::get_next_event_position_raw() const noexcept {
  util::const_byte_span remainder{
      portion_.subspan(next_event_position_offset, sizeof(std::uint32_t))};
  std::uint32_t next_event_position{};
  util::extract_fixed_int_from_byte_span(remainder, next_event_position);
  return next_event_position;
}

void common_header_view_base::set_next_event_position_raw(
    std::uint32_t next_event_position) const noexcept {
  util::byte_span remainder{
      portion_.subspan(next_event_position_offset, sizeof next_event_position)};
  util::insert_fixed_int_to_byte_span(remainder, next_event_position);
}

[[nodiscard]] std::uint16_t
common_header_view_base::get_flags_raw() const noexcept {
  util::const_byte_span remainder{
      portion_.subspan(flags_offset, sizeof(std::uint16_t))};
  std::uint16_t flags{};
  util::extract_fixed_int_from_byte_span(remainder, flags);
  return flags;
}
[[nodiscard]] common_header_flag_set
common_header_view_base::get_flags() const noexcept {
  return get_flags_from_raw(get_flags_raw());
}

void common_header_view_base::set_flags_raw(
    std::uint16_t flags) const noexcept {
  util::byte_span remainder{portion_.subspan(flags_offset, sizeof flags)};
  util::insert_fixed_int_to_byte_span(remainder, flags);
}

std::ostream &operator<<(std::ostream &output, const common_header_view &obj) {
  return output << "ts: " << obj.get_readable_timestamp()
                << ", type: " << obj.get_readable_type_code()
                << ", server id: " << obj.get_server_id_raw()
                << ", event size: " << obj.get_event_size_raw()
                << ", next event position: "
                << obj.get_next_event_position_raw()
                << ", flags: " << obj.get_readable_flags();
}

} // namespace binsrv::events
