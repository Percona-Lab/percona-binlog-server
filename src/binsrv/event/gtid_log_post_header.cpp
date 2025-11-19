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

#include "binsrv/event/gtid_log_post_header.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <string>
#include <tuple>

#include <boost/align/align_up.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "binsrv/event/gtid_log_flag_type.hpp"

#include "binsrv/gtid/common_types.hpp"

#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/flag_set.hpp"

namespace binsrv::event {

gtid_log_post_header::gtid_log_post_header(util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  /*
    https://github.com/mysql/mysql-server/blob/mysql-8.0.43/libbinlogevents/include/control_events.h#L918
    https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.h#L927

    <tr>
      <td>GTID_FLAGS</td>
      <td>1 byte</td>
      <td>00000001 = Transaction may have changes logged with SBR.
          In 5.6, 5.7.0-5.7.18, and 8.0.0-8.0.1, this flag is always set.
          Starting in 5.7.19 and 8.0.2, this flag is cleared if the transaction
          only contains row events. It is set if any part of the transaction is
          written in statement format.</td>
    </tr>
    <tr>
      <td>SID</td>
      <td>16 byte sequence</td>
      <td>UUID representing the SID</td>
    </tr>
    <tr>
      <td>GNO</td>
      <td>8 byte integer</td>
      <td>Group number, second component of GTID.</td>
    </tr>
    <tr>
      <td>logical clock timestamp typecode</td>
      <td>1 byte integer</td>
      <td>The type of logical timestamp used in the logical clock fields.</td>
    </tr>
    <tr>
      <td>last_committed</td>
      <td>8 byte integer</td>
      <td>Store the transaction's commit parent sequence_number</td>
    </tr>
    <tr>
      <td>sequence_number</td>
      <td>8 byte integer</td>
      <td>The transaction's logical timestamp assigned at prepare phase</td>
    </tr>
  */

  // TODO: initialize size_in_bytes directly based on the sum of fields
  // widths instead of this static_assert
  static_assert(sizeof flags_ + std::tuple_size_v<decltype(uuid_)> +
                        sizeof gno_ + sizeof logical_ts_code_ +
                        sizeof last_committed_ + sizeof sequence_number_ ==
                    size_in_bytes,
                "mismatch in gtid_log_event_post_header::size_in_bytes");
  // make sure we did OK with data members reordering
  static_assert(
      sizeof *this ==
          boost::alignment::align_up(size_in_bytes, alignof(decltype(*this))),
      "inefficient data member reordering in gtid_log_event_post_header");

  if (std::size(portion) != size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid gtid_log event post-header length");
  }

  // https://github.com/mysql/mysql-server/blob/mysql-8.0.43/libbinlogevents/src/control_events.cpp#L428
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.cpp#L461
  auto remainder = portion;
  util::extract_fixed_int_from_byte_span(remainder, flags_);
  util::extract_byte_array_from_byte_span(remainder, uuid_);
  util::extract_fixed_int_from_byte_span(remainder, gno_);

  // TODO: for gtid_log (not anonymous gtid_log) add validation of gno -
  //       (gno >= gtid::min_gno && gno_ < gtid::max_gno)
  util::extract_fixed_int_from_byte_span(remainder, logical_ts_code_);
  if (logical_ts_code_ != expected_logical_ts_code) {
    util::exception_location().raise<std::invalid_argument>(
        "unsupported logical timestamp code in gtid_log post-header");
  }
  util::extract_fixed_int_from_byte_span(remainder, last_committed_);
  util::extract_fixed_int_from_byte_span(remainder, sequence_number_);
}

[[nodiscard]] gtid_log_flag_set
gtid_log_post_header::get_flags() const noexcept {
  return gtid_log_flag_set{get_flags_raw()};
}

[[nodiscard]] std::string gtid_log_post_header::get_readable_flags() const {
  return to_string(get_flags());
}

[[nodiscard]] gtid::uuid gtid_log_post_header::get_uuid() const noexcept {
  gtid::uuid result;
  const auto &uuid_raw{get_uuid_raw()};
  static_assert(std::tuple_size_v<decltype(uuid_)> ==
                boost::uuids::uuid::static_size());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  std::copy_n(reinterpret_cast<const boost::uuids::uuid::value_type *>(
                  std::data(uuid_raw)),
              boost::uuids::uuid::static_size(), std::begin(result));
  return result;
}

[[nodiscard]] std::string gtid_log_post_header::get_readable_uuid() const {
  return boost::uuids::to_string(get_uuid());
}

std::ostream &operator<<(std::ostream &output,
                         const gtid_log_post_header &obj) {
  return output << "flags: " << obj.get_readable_flags()
                << ", uuid: " << obj.get_readable_uuid()
                << ", gno: " << obj.get_gno_raw() << ", logical_ts_code: "
                << static_cast<std::uint32_t>(obj.get_logical_ts_code_raw())
                << ", last_committed: " << obj.get_last_committed_raw()
                << ", sequence_number: " << obj.get_sequence_number_raw();
}

} // namespace binsrv::event
