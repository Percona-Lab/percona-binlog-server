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

#include "binsrv/event/format_description_post_header_impl.hpp"

#include <cstdint>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/align/align_up.hpp>

#include <boost/date_time/posix_time/conversion.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>

#include "binsrv/event/code_type.hpp"
#include "binsrv/event/protocol_traits.hpp"

#include "util/bounded_string_storage.hpp"
#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/semantic_version.hpp"

namespace binsrv::event {

generic_post_header_impl<code_type::format_description>::
    generic_post_header_impl(std::uint32_t encoded_server_version,
                             util::const_byte_span portion) {
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

  if (std::size(portion) != get_size_in_bytes(encoded_server_version)) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid format_description event post-header length");
  }

  auto remainder = portion;
  util::extract_fixed_int_from_byte_span(remainder, binlog_version_);
  server_version_.resize(decltype(server_version_)::capacity());
  util::extract_byte_span_from_byte_span(remainder,
                                         util::byte_span{server_version_});
  util::normalize_for_c_str(server_version_);
  util::extract_fixed_int_from_byte_span(remainder, create_timestamp_);
  util::extract_fixed_int_from_byte_span(remainder, common_header_length_);
  const auto expected_subrange_length{
      get_number_of_events(encoded_server_version) - 1U};
  const std::span<encoded_post_header_length_type> post_header_lengths_subrange{
      std::data(post_header_lengths_), expected_subrange_length};
  util::extract_byte_span_from_byte_span(remainder,
                                         post_header_lengths_subrange);
}

[[nodiscard]] std::string_view
generic_post_header_impl<code_type::format_description>::get_server_version()
    const noexcept {
  return util::to_string_view(server_version_);
}

[[nodiscard]] std::uint32_t generic_post_header_impl<
    code_type::format_description>::get_encoded_server_version()
    const noexcept {
  return util::semantic_version{get_server_version()}.get_encoded();
}

[[nodiscard]] std::string generic_post_header_impl<
    code_type::format_description>::get_readable_create_timestamp() const {
  return boost::posix_time::to_simple_string(
      boost::posix_time::from_time_t(get_create_timestamp()));
}

std::ostream &
operator<<(std::ostream &output,
           const generic_post_header_impl<code_type::format_description> &obj) {
  output << "binlog version: " << obj.get_binlog_version_raw()
         << ", server version: " << obj.get_server_version()
         << ", create timestamp: " << obj.get_readable_create_timestamp()
         << ", common header length: " << obj.get_common_header_length()
         << "\npost-header lengths: {\n";
  print_post_header_lengths(output, obj.get_encoded_server_version(),
                            obj.get_post_header_lengths_raw());
  output << "\n}";
  return output;
}

} // namespace binsrv::event
