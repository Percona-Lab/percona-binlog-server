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

#include "binsrv/events/format_description_post_header_impl.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/align/align_up.hpp>

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/protocol_traits.hpp"

#include "util/bounded_string_storage.hpp"
#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/byte_span_inserters.hpp"
#include "util/ctime_timestamp.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/semantic_version.hpp"

namespace binsrv::events {

generic_post_header_impl<code_type::format_description>::
    generic_post_header_impl(
        std::uint16_t binlog_version,
        const util::semantic_version &server_version,
        const util::ctime_timestamp &create_timestamp,
        std::size_t common_header_length,
        const post_header_length_container &post_header_lengths)
    : create_timestamp_{static_cast<std::uint32_t>(
          create_timestamp.get_value())},
      server_version_{}, binlog_version_{binlog_version},
      post_header_lengths_{post_header_lengths},
      common_header_length_{static_cast<std::uint8_t>(common_header_length)} {
  const auto readable_server_version{server_version.get_string()};
  const auto truncated_length{
      std::min(std::size(readable_server_version),
               server_version_storage::static_capacity)};
  const std::string_view readable_server_version_sv(
      readable_server_version.c_str(), truncated_length);
  const auto readable_server_version_span{
      util::as_const_byte_span(readable_server_version_sv)};
  server_version_.assign(std::cbegin(readable_server_version_span),
                         std::cend(readable_server_version_span));
}

generic_post_header_impl<code_type::format_description>::
    generic_post_header_impl(util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  /*
    https://github.com/mysql/mysql-server/blob/mysql-8.0.43/libbinlogevents/include/control_events.h#L287
    https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.h#L295
    +=====================================+
    | event  | binlog_version   19 : 2    | = 4
    | data   +----------------------------+
    |        | server_version   21 : 50   |
    |        +----------------------------+
    |        | create_timestamp 71 : 4    |
    |        +----------------------------+
    |        | header_length    75 : 1    |
    |        +----------------------------+
    |        | post-header      76 : n    | = array of n bytes, one byte
    |        | lengths for all            |   per event type that the
    |        | event types                |   server knows about
    +=====================================+
  */

  static_assert(server_version_length ==
                    decltype(server_version_)::static_capacity,
                "mismatch in "
                "generic_post_header_impl<code_type::format_description>::"
                "server_version_length");

  // TODO: initialize size_in_bytes directly based on the sum of fields
  // widths instead of this static_assert
  static_assert(
      sizeof binlog_version_ + decltype(server_version_)::static_capacity +
              sizeof create_timestamp_ + sizeof common_header_length_ +
              sizeof post_header_lengths_ ==
          get_size_in_bytes(latest_known_protocol_server_version),
      "mismatch in "
      "generic_post_header_impl<code_type::format_description>::size_in_bytes");
  // make sure we did OK with data members reordering
  static_assert(sizeof *this ==
                    boost::alignment::align_up(
                        sizeof binlog_version_ + sizeof server_version_ +
                            sizeof create_timestamp_ +
                            sizeof common_header_length_ +
                            sizeof post_header_lengths_,
                        alignof(decltype(*this))),
                "inefficient data member reordering in "
                "generic_post_header_impl<code_type::format_description>");

  if (std::size(portion) <
          get_size_in_bytes(earliest_supported_protocol_server_version) ||
      std::size(portion) >
          get_size_in_bytes(latest_known_protocol_server_version)) {
    util::exception_location().raise<std::invalid_argument>(
        "format_description event post header length is not in allowed range");
  }

  auto remainder = portion;
  util::extract_fixed_int_from_byte_span(remainder, binlog_version_);
  server_version_.resize(decltype(server_version_)::static_capacity);
  util::extract_byte_span_from_byte_span(remainder,
                                         util::byte_span{server_version_});
  util::normalize_for_c_str(server_version_);

  const auto encoded_server_version{get_encoded_server_version()};
  if (std::size(portion) != get_size_in_bytes(encoded_server_version)) {
    util::exception_location().raise<std::invalid_argument>(
        "format_description event post header length does not match the value "
        "expected for the server version specified in its own fields");
  }

  util::extract_fixed_int_from_byte_span(remainder, create_timestamp_);
  util::extract_fixed_int_from_byte_span(remainder, common_header_length_);
  const auto expected_subrange_length{
      get_number_of_events(encoded_server_version) - 1U};
  const std::span<encoded_post_header_length_type> post_header_lengths_subrange{
      std::data(post_header_lengths_), expected_subrange_length};
  util::extract_byte_span_from_byte_span(remainder,
                                         post_header_lengths_subrange);
  if (std::size(portion) != get_post_header_length_for_code(
                                encoded_server_version, post_header_lengths_,
                                code_type::format_description)) {
    util::exception_location().raise<std::invalid_argument>(
        "format_description event post header length does not match the value "
        "specified in the post header length array in its own fields");
  }
}

[[nodiscard]] std::string_view generic_post_header_impl<
    code_type::format_description>::get_readable_server_version()
    const noexcept {
  return util::to_string_view(server_version_);
}
[[nodiscard]] util::semantic_version
generic_post_header_impl<code_type::format_description>::get_server_version()
    const {
  return util::semantic_version{get_readable_server_version()};
}

[[nodiscard]] std::uint32_t generic_post_header_impl<
    code_type::format_description>::get_encoded_server_version()
    const noexcept {
  return get_server_version().get_encoded();
}

[[nodiscard]] util::ctime_timestamp
generic_post_header_impl<code_type::format_description>::get_create_timestamp()
    const noexcept {
  return util::ctime_timestamp{
      static_cast<std::time_t>(get_create_timestamp_raw())};
}

[[nodiscard]] std::string generic_post_header_impl<
    code_type::format_description>::get_readable_create_timestamp() const {
  return get_create_timestamp().simple_str();
}

void generic_post_header_impl<code_type::format_description>::encode_to(
    util::byte_span &destination) const {
  if (std::size(destination) < calculate_encoded_size()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot encode format description event post header");
  }
  util::insert_fixed_int_to_byte_span(destination, get_binlog_version_raw());

  const util::const_byte_span server_version_span{get_server_version_raw()};
  util::insert_byte_span_to_byte_span(destination, server_version_span);
  const server_version_storage zero_filled_buffer(
      server_version_storage::static_capacity - std::size(server_version_span));
  util::insert_byte_span_to_byte_span(
      destination, util::const_byte_span{zero_filled_buffer});

  util::insert_fixed_int_to_byte_span(destination, get_create_timestamp_raw());
  util::insert_fixed_int_to_byte_span(destination,
                                      get_common_header_length_raw());
  const auto expected_subrange_length{
      get_number_of_events(get_encoded_server_version()) - 1U};
  const std::span<const encoded_post_header_length_type>
      post_header_lengths_subrange{std::data(get_post_header_lengths_raw()),
                                   expected_subrange_length};
  util::insert_byte_span_to_byte_span(destination,
                                      post_header_lengths_subrange);
}

std::ostream &
operator<<(std::ostream &output,
           const generic_post_header_impl<code_type::format_description> &obj) {
  output << "binlog version: " << obj.get_binlog_version_raw()
         << ", server version: " << obj.get_readable_server_version()
         << ", create timestamp: " << obj.get_readable_create_timestamp()
         << ", common header length: " << obj.get_common_header_length()
         << "\npost header lengths: {\n";
  print_post_header_lengths(output, obj.get_encoded_server_version(),
                            obj.get_post_header_lengths_raw());
  output << "\n}";
  return output;
}

} // namespace binsrv::events
