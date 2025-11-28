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

#include "binsrv/gtids/gtid.hpp"

#include <ostream>
#include <stdexcept>

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/tag_fwd.hpp"
#include "binsrv/gtids/uuid.hpp"

#include "util/exception_location_helpers.hpp"

namespace binsrv::gtids {

void gtid::validate_components(const uuid &uuid_component,
                               const tag & /*tag_component*/,
                               gno_t gno_component) {
  if (uuid_component.is_empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "uuid must not be empty");
  }
  if (gno_component < min_gno || gno_component > max_gno) {
    util::exception_location().raise<std::invalid_argument>(
        "gno is out of range");
  }
}

std::ostream &operator<<(std::ostream &output, const gtid &obj) {
  output << obj.get_uuid();
  if (obj.has_tag()) {
    output << gtid::tag_separator << obj.get_tag();
  }
  output << gtid::gno_separator << obj.get_gno();
  return output;
}

} // namespace binsrv::gtids
