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

#include "binsrv/event/rotate_post_header_impl.hpp"

#include <iterator>
#include <ostream>
#include <stdexcept>

#include <boost/align/align_up.hpp>

#include "binsrv/event/code_type.hpp"

#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

generic_post_header_impl<code_type::rotate>::generic_post_header_impl(
    util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  /*
    https://github.com/mysql/mysql-server/blob/mysql-8.0.36/libbinlogevents/include/control_events.h#L53

    <table>
    <caption>Post-Header for Rotate_event</caption>

    <tr>
      <th>Name</th>
      <th>Format</th>
      <th>Description</th>
    </tr>

    <tr>
      <td>position</td>
      <td>8 byte integer</td>
      <td>The position within the binary log to rotate to.</td>
    </tr>

    </table>
  */

  // TODO: initialize size_in_bytes directly based on the sum of fields
  // widths instead of this static_assert
  static_assert(sizeof position_ == size_in_bytes,
                "mismatch in rotate_event_post_header::size_in_bytes");
  // make sure we did OK with data members reordering
  static_assert(
      sizeof *this ==
          boost::alignment::align_up(size_in_bytes, alignof(decltype(*this))),
      "inefficient data member reordering in rotate_event_post_header");

  if (std::size(portion) != size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid rotate event post-header length");
  }

  auto remainder = portion;
  util::extract_fixed_int_from_byte_span(remainder, position_);
}

std::ostream &
operator<<(std::ostream &output,
           const generic_post_header_impl<code_type::rotate> &obj) {
  return output << "position: " << obj.get_position_raw();
}

} // namespace binsrv::event
