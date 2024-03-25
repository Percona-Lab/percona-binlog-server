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

#include "binsrv/event/empty_post_header.hpp"

#include <iosfwd>
#include <iterator>
#include <stdexcept>

#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

empty_post_header::empty_post_header(util::const_byte_span portion) {
  if (std::size(portion) != size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid event empty post header length");
  }
}

std::ostream &operator<<(std::ostream &output,
                         const empty_post_header & /* obj */) {
  return output;
}

} // namespace binsrv::event
