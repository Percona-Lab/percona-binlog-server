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

#include "binsrv/event/gtid_log_body_impl.hpp"

#include <cstdint>
#include <ctime>
#include <ostream>
#include <stdexcept>
#include <string>

#include <boost/align/align_up.hpp>

#include <boost/date_time/posix_time/conversion.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/posix_time/time_formatters_limited.hpp>

#include "binsrv/event/code_type.hpp"

#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/semantic_version.hpp"

namespace binsrv::event {

generic_body_impl<code_type::gtid_log>::generic_body_impl(
    util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  // make sure we did OK with data members reordering
  static_assert(sizeof *this == boost::alignment::align_up(
                                    sizeof immediate_commit_timestamp_ +
                                        sizeof original_commit_timestamp_ +
                                        sizeof transaction_length_ +
                                        sizeof original_server_version_ +
                                        sizeof immediate_server_version_ +
                                        sizeof commit_group_ticket_,
                                    alignof(decltype(*this))),
                "inefficient data member reordering in gtid_log event body");

  // https://github.com/mysql/mysql-server/blob/mysql-8.0.43/libbinlogevents/src/control_events.cpp#L513
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.cpp#L558
  auto remainder = portion;

  if (!util::extract_fixed_int_from_byte_span_checked(
          remainder, immediate_commit_timestamp_,
          commit_timestamp_field_length)) {
    util::exception_location().raise<std::invalid_argument>(
        "gtid_log event body is too short to extract immediate commit "
        "timestamp");
  }
  const std::uint64_t commit_timestamp_mask{
      1ULL << (commit_timestamp_field_length * 8ULL - 1ULL)};
  if ((immediate_commit_timestamp_ & commit_timestamp_mask) != 0ULL) {
    // clearing the most significant bit in the
    // commit_timestamp_field_length-byte sequence
    original_commit_timestamp_ &= ~commit_timestamp_mask;
    if (!util::extract_fixed_int_from_byte_span_checked(
            remainder, original_commit_timestamp_,
            commit_timestamp_field_length)) {
      util::exception_location().raise<std::invalid_argument>(
          "gtid_log event body is too short to extract original commit "
          "timestamp");
    }
  }

  if (!util::extract_packed_int_from_byte_span_checked(remainder,
                                                       transaction_length_)) {
    util::exception_location().raise<std::invalid_argument>(
        "unable to extract transaction length from the gtid_log event body");
  }

  if (std::size(remainder) >= server_version_field_length) {
    util::extract_fixed_int_from_byte_span(remainder, original_server_version_);
    const std::uint32_t server_version_mask{
        1ULL << (server_version_field_length * 8ULL - 1ULL)};
    if ((original_server_version_ & server_version_mask) != 0UL) {
      original_server_version_ &= ~server_version_mask;
      if (!util::extract_fixed_int_from_byte_span_checked(
              remainder, immediate_server_version_)) {
        util::exception_location().raise<std::invalid_argument>(
            "gtid_log event body is too short to extract immediate server "
            "version");
      }
    }
    if (std::size(remainder) >= commit_group_ticket_field_length) {
      util::extract_fixed_int_from_byte_span(remainder, commit_group_ticket_);
    }
  }
  if (std::size(remainder) != 0U) {
    util::exception_location().raise<std::invalid_argument>(
        "extra bytes in the gtid_log event body");
  }
}
[[nodiscard]] std::string generic_body_impl<
    code_type::gtid_log>::get_readable_immediate_commit_timestamp() const {
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
generic_body_impl<code_type::gtid_log>::get_original_server_version()
    const noexcept {
  return util::semantic_version{get_original_server_version_raw()};
}

[[nodiscard]] std::string
generic_body_impl<code_type::gtid_log>::get_readable_original_server_version()
    const {
  return get_original_server_version().get_string();
}

[[nodiscard]] util::semantic_version
generic_body_impl<code_type::gtid_log>::get_immediate_server_version()
    const noexcept {
  return util::semantic_version{get_immediate_server_version_raw()};
}

[[nodiscard]] std::string
generic_body_impl<code_type::gtid_log>::get_readable_immediate_server_version()
    const {
  return get_immediate_server_version().get_string();
}

std::ostream &operator<<(std::ostream &output,
                         const generic_body_impl<code_type::gtid_log> &obj) {
  output << "immediate_commit_timestamp: "
         << obj.get_readable_immediate_commit_timestamp();
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

} // namespace binsrv::event
