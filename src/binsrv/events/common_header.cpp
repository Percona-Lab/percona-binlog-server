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

#include "binsrv/events/common_header.hpp"

#include <ostream>

#include <boost/align/align_up.hpp>

#include "binsrv/ctime_timestamp.hpp"

#include "binsrv/events/common_header_flag_type.hpp"
#include "binsrv/events/common_header_view.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::events {

common_header::common_header(const common_header_view &view)
    : timestamp_{view.get_timestamp_raw()},
      server_id_{view.get_server_id_raw()},
      event_size_{view.get_event_size_raw()},
      next_event_position_{view.get_next_event_position_raw()},
      flags_{view.get_flags_raw()}, type_code_{view.get_type_code_raw()} {
  // TODO: initialize size_in_bytes directly based on the sum of fields
  // widths instead of this static_assert
  static_assert(sizeof timestamp_ + sizeof type_code_ + sizeof server_id_ +
                        sizeof event_size_ + sizeof next_event_position_ +
                        sizeof flags_ ==
                    size_in_bytes,
                "mismatch in common_header::size_in_bytes");
  // make sure we did OK with data members reordering
  static_assert(sizeof *this == boost::alignment::align_up(
                                    size_in_bytes, alignof(decltype(*this))),
                "inefficient data member reordering in common_header");
}

common_header::common_header(util::const_byte_span portion)
    : common_header{common_header_view{portion}} {}

[[nodiscard]] ctime_timestamp common_header::get_timestamp() const noexcept {
  return common_header_view_base::get_timestamp_from_raw(get_timestamp_raw());
}

[[nodiscard]] common_header_flag_set common_header::get_flags() const noexcept {
  return common_header_view_base::get_flags_from_raw(get_flags_raw());
}

std::ostream &operator<<(std::ostream &output, const common_header &obj) {
  return output << "ts: " << obj.get_readable_timestamp()
                << ", type: " << obj.get_readable_type_code()
                << ", server id: " << obj.get_server_id_raw()
                << ", event size: " << obj.get_event_size_raw()
                << ", next event position: "
                << obj.get_next_event_position_raw()
                << ", flags: " << obj.get_readable_flags();
}

} // namespace binsrv::events
