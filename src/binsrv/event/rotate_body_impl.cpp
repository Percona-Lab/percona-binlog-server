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

#include "binsrv/event/rotate_body_impl.hpp"

#include <ostream>

#include "binsrv/event/code_type.hpp"

#include "util/byte_span.hpp"

namespace binsrv::event {

generic_body_impl<code_type::rotate>::generic_body_impl(
    util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  // no need to check if member reordering is OK as this class has
  // only one member for holding data of varying length

  binlog_.assign(util::as_string_view(portion));
}

std::ostream &operator<<(std::ostream &output,
                         const generic_body_impl<code_type::rotate> &obj) {
  return output << "binlog: " << obj.get_binlog();
}

} // namespace binsrv::event
