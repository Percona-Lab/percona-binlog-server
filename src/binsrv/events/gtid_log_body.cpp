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

#include "binsrv/events/gtid_log_body.hpp"

#include <cstdint>
#include <ctime>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>

#include <boost/align/align_up.hpp>

#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/byte_span_inserters.hpp"
#include "util/common_optional_types.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/semantic_version.hpp"
#include "util/timestamp_helpers.hpp"

namespace binsrv::events {

gtid_log_body::gtid_log_body(
    const util::high_resolution_time_point &immediate_commit_timestamp,
    const util::optional_high_resolution_time_point &original_commit_timestamp,
    std::uint64_t transaction_length,
    const util::optional_semantic_version &immediate_server_version,
    const util::optional_semantic_version &original_server_version,
    const util::optional_uint64_t &commit_group_ticket) noexcept
    : immediate_commit_timestamp_(
          util::high_resolution_time_point_to_microseconds(
              immediate_commit_timestamp)),
      original_commit_timestamp_{
          original_commit_timestamp
              ? util::high_resolution_time_point_to_microseconds(
                    *original_commit_timestamp)
              : unset_commit_timestamp},
      transaction_length_{transaction_length},
      immediate_server_version_{immediate_server_version
                                    ? immediate_server_version->get_encoded()
                                    : unset_server_version},
      original_server_version_{original_server_version
                                   ? original_server_version->get_encoded()
                                   : unset_server_version},
      commit_group_ticket_{commit_group_ticket ? *commit_group_ticket
                                               : unset_commit_group_ticket} {}

gtid_log_body::gtid_log_body(util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  // make sure we did OK with data members reordering
  static_assert(sizeof *this == boost::alignment::align_up(
                                    sizeof immediate_commit_timestamp_ +
                                        sizeof original_commit_timestamp_ +
                                        sizeof transaction_length_ +
                                        sizeof immediate_server_version_ +
                                        sizeof original_server_version_ +
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
  static constexpr std::uint64_t commit_timestamp_mask{
      1ULL << (commit_timestamp_field_length * 8ULL - 1ULL)};
  if ((immediate_commit_timestamp_ & commit_timestamp_mask) != 0ULL) {
    // clearing the most significant bit in the
    // commit_timestamp_field_length-byte sequence
    immediate_commit_timestamp_ &= ~commit_timestamp_mask;
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
    util::extract_fixed_int_from_byte_span(remainder,
                                           immediate_server_version_);
    static constexpr std::uint32_t server_version_mask{
        1U << (server_version_field_length * 8U - 1U)};
    if ((immediate_server_version_ & server_version_mask) != 0U) {
      immediate_server_version_ &= ~server_version_mask;
      if (!util::extract_fixed_int_from_byte_span_checked(
              remainder, original_server_version_)) {
        util::exception_location().raise<std::invalid_argument>(
            "gtid_log event body is too short to extract original server "
            "version");
      }
    }
    if (std::size(remainder) >= commit_group_ticket_field_length) {
      util::extract_fixed_int_from_byte_span(remainder, commit_group_ticket_);
    }
  }
  if (!remainder.empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "extra bytes in the gtid_log event body");
  }
}
[[nodiscard]] std::string
gtid_log_body::get_readable_immediate_commit_timestamp() const {
  return util::microseconds_to_iso_extended_string(
      get_immediate_commit_timestamp_raw());
}

[[nodiscard]] std::string
gtid_log_body::get_readable_original_commit_timestamp() const {
  if (!has_original_commit_timestamp()) {
    return {};
  }
  return util::microseconds_to_iso_extended_string(
      get_original_commit_timestamp_raw());
}

[[nodiscard]] util::optional_semantic_version
gtid_log_body::get_immediate_server_version() const noexcept {
  if (!has_immediate_server_version()) {
    return std::nullopt;
  }
  return util::semantic_version{get_immediate_server_version_raw()};
}

[[nodiscard]] std::string
gtid_log_body::get_readable_immediate_server_version() const {
  const auto immediate_server_version{get_immediate_server_version()};
  if (!immediate_server_version) {
    return {};
  }
  return immediate_server_version->get_string();
}

[[nodiscard]] util::optional_semantic_version
gtid_log_body::get_original_server_version() const noexcept {
  if (!has_original_server_version()) {
    return std::nullopt;
  }
  return util::semantic_version{get_original_server_version_raw()};
}

[[nodiscard]] std::string
gtid_log_body::get_readable_original_server_version() const {
  const auto original_server_version{get_original_server_version()};
  if (!original_server_version) {
    return {};
  }
  return original_server_version->get_string();
}

[[nodiscard]] std::size_t
gtid_log_body::calculate_encoded_size() const noexcept {
  std::size_t result{0U};
  result += commit_timestamp_field_length;
  result +=
      (has_original_commit_timestamp() ? commit_timestamp_field_length : 0U);
  result += util::calculate_packed_int_size(transaction_length_);
  if (has_immediate_server_version()) {
    result += server_version_field_length;
    result +=
        (has_original_server_version() ? server_version_field_length : 0U);
    result +=
        (has_commit_group_ticket() ? commit_group_ticket_field_length : 0U);
  }

  return result;
}

void gtid_log_body::encode_to(util::byte_span &destination) const {
  if (std::size(destination) < calculate_encoded_size()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot encode rotate event body");
  }

  static constexpr std::uint64_t commit_timestamp_mask{
      1ULL << (commit_timestamp_field_length * 8ULL - 1ULL)};

  const std::uint64_t patched_immediate_commit_timestamp{
      immediate_commit_timestamp_ |
      (has_original_commit_timestamp() ? commit_timestamp_mask : 0ULL)};
  util::insert_fixed_int_to_byte_span(destination,
                                      patched_immediate_commit_timestamp,
                                      commit_timestamp_field_length);
  if (has_original_commit_timestamp()) {
    util::insert_fixed_int_to_byte_span(destination, original_commit_timestamp_,
                                        commit_timestamp_field_length);
  }
  [[maybe_unused]] const auto insertion_result{
      util::insert_packed_int_to_byte_span_checked(destination,
                                                   transaction_length_)};

  if (has_immediate_server_version()) {
    static constexpr std::uint32_t server_version_mask{
        1U << (server_version_field_length * 8U - 1U)};
    const std::uint32_t patched_immediate_server_version{
        immediate_server_version_ |
        (has_original_server_version() ? server_version_mask : 0U)};
    util::insert_fixed_int_to_byte_span(destination,
                                        patched_immediate_server_version);
    if (has_original_server_version()) {
      util::insert_fixed_int_to_byte_span(destination,
                                          original_server_version_);
    }
    if (has_commit_group_ticket()) {
      util::insert_fixed_int_to_byte_span(destination, commit_group_ticket_);
    }
  }
}

std::ostream &operator<<(std::ostream &output, const gtid_log_body &obj) {
  output << "immediate_commit_timestamp: "
         << obj.get_readable_immediate_commit_timestamp();
  if (obj.has_original_commit_timestamp()) {
    output << ", original_commit_timestamp: "
           << obj.get_readable_original_commit_timestamp();
  }
  output << ", transaction_length: " << obj.get_transaction_length_raw();
  if (obj.has_immediate_server_version()) {
    output << ", immediate_server_version: "
           << obj.get_readable_immediate_server_version();
  }
  if (obj.has_original_server_version()) {
    output << ", original_server_version: "
           << obj.get_readable_original_server_version();
  }
  if (obj.has_commit_group_ticket()) {
    output << ", commit_group_ticket: " << obj.get_commit_group_ticket_raw();
  }

  return output;
}

} // namespace binsrv::events
