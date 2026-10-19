// Copyright (c) 2026 Percona and/or its affiliates.
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

#include "binsrv/events/heartbeat_log_v2_body_impl.hpp"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <stdexcept>

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/composite_binlog_name.hpp"

#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/byte_span_inserters.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::events {

namespace {

// TLV field identifiers, matching upstream's
// mysql::binlog::event::codecs::binary::Heartbeat::fields:
// https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/codecs/binary.h#L125
constexpr std::uint64_t heartbeat_field_end_marker{0U};
constexpr std::uint64_t heartbeat_field_log_filename{1U};
constexpr std::uint64_t heartbeat_field_log_position{2U};

} // namespace

generic_body_impl<code_type::heartbeat_log_v2>::generic_body_impl(
    const composite_binlog_name &binlog_name, std::uint64_t position)
    : position_{position} {
  const auto binlog_name_s{binlog_name.str()};
  const auto binlog_name_span{util::as_const_byte_span(binlog_name_s)};
  binlog_.assign(std::cbegin(binlog_name_span), std::cend(binlog_name_span));
}

generic_body_impl<code_type::heartbeat_log_v2>::generic_body_impl(
    util::const_byte_span portion) {
  // The decoder mirrors upstream's Heartbeat::decode: it walks a sequence of
  // <type> <length> <value> triples (each a packed integer) until it reaches
  // the end marker or runs out of data. Unknown field types are skipped by
  // their declared length, which keeps the format forward compatible.
  auto remainder = portion;
  for (;;) {
    // a well-formed body ends with the end marker handled below; reaching the
    // end of the data without one is tolerated, matching upstream's decoder
    // which loops only while bytes remain
    if (remainder.empty()) {
      break;
    }
    std::uint64_t field_type{};
    if (!util::extract_packed_int_from_byte_span_checked(remainder,
                                                         field_type)) {
      // the span is not empty but the field type could not be decoded (a
      // truncated or forbidden packed integer) - treat it as corruption, like
      // every other malformed field below
      util::exception_location().raise<std::invalid_argument>(
          "heartbeat_log_v2 event body holds a malformed field type");
    }
    if (field_type == heartbeat_field_end_marker) {
      break;
    }
    std::uint64_t field_length{};
    if (!util::extract_packed_int_from_byte_span_checked(remainder,
                                                         field_length)) {
      util::exception_location().raise<std::invalid_argument>(
          "heartbeat_log_v2 event body is too short to extract a field length");
    }
    switch (field_type) {
    case heartbeat_field_log_filename: {
      if (field_length > std::size(remainder)) {
        util::exception_location().raise<std::invalid_argument>(
            "heartbeat_log_v2 event body is too short to extract the log "
            "filename");
      }
      binlog_.resize(field_length);
      const std::span<std::byte> binlog_subrange{binlog_};
      // the preceding length check guarantees enough bytes remain, so the
      // unchecked extractor is safe here
      util::extract_byte_span_from_byte_span(remainder, binlog_subrange);
    } break;
    case heartbeat_field_log_position:
      // the length field holds the packed size of the position; the position
      // itself is re-read as a packed integer, exactly as upstream does
      if (!util::extract_packed_int_from_byte_span_checked(remainder,
                                                           position_)) {
        util::exception_location().raise<std::invalid_argument>(
            "heartbeat_log_v2 event body is too short to extract the log "
            "position");
      }
      break;
    default:
      if (field_length > std::size(remainder)) {
        util::exception_location().raise<std::invalid_argument>(
            "heartbeat_log_v2 event body is too short to skip an unknown "
            "field");
      }
      remainder = remainder.subspan(field_length);
      break;
    }
  }
}

[[nodiscard]] composite_binlog_name
generic_body_impl<code_type::heartbeat_log_v2>::get_parsed_binlog() const {
  return composite_binlog_name::parse(get_readable_binlog());
}

[[nodiscard]] std::size_t
generic_body_impl<code_type::heartbeat_log_v2>::calculate_encoded_size()
    const noexcept {
  const auto position_packed_size{util::calculate_packed_int_size(position_)};
  return util::calculate_packed_int_size(heartbeat_field_log_filename) +
         util::calculate_packed_int_size(std::size(binlog_)) +
         std::size(binlog_) +
         util::calculate_packed_int_size(heartbeat_field_log_position) +
         util::calculate_packed_int_size(position_packed_size) +
         position_packed_size +
         util::calculate_packed_int_size(heartbeat_field_end_marker);
}

void generic_body_impl<code_type::heartbeat_log_v2>::encode_to(
    util::byte_span &destination) const {
  if (std::size(destination) < calculate_encoded_size()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot encode heartbeat_log_v2 event body");
  }
  // It is OK to ignore the inserter results here as we already ensured that
  // the destination is large enough.
  [[maybe_unused]] bool insert_result{};

  // log filename field: type, length, raw bytes
  insert_result = util::insert_packed_int_to_byte_span_checked(
      destination, heartbeat_field_log_filename);
  insert_result = util::insert_packed_int_to_byte_span_checked(
      destination, std::size(binlog_));
  const util::const_byte_span binlog_span{binlog_};
  util::insert_byte_span_to_byte_span(destination, binlog_span);

  // log position field: type, length (packed size of the position), the
  // position itself as a packed integer
  insert_result = util::insert_packed_int_to_byte_span_checked(
      destination, heartbeat_field_log_position);
  insert_result = util::insert_packed_int_to_byte_span_checked(
      destination, util::calculate_packed_int_size(position_));
  insert_result =
      util::insert_packed_int_to_byte_span_checked(destination, position_);

  // end-of-fields marker
  insert_result = util::insert_packed_int_to_byte_span_checked(
      destination, heartbeat_field_end_marker);
}

std::ostream &
operator<<(std::ostream &output,
           const generic_body_impl<code_type::heartbeat_log_v2> &obj) {
  return output << "binlog: " << obj.get_readable_binlog()
                << ", position: " << obj.get_position();
}

} // namespace binsrv::events
