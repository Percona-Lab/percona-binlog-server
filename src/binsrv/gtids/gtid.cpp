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

#include <cstddef>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/tag_fwd.hpp"
#include "binsrv/gtids/uuid.hpp"

#include "util/exception_location_helpers.hpp"

namespace binsrv::gtids {

[[nodiscard]] std::string gtid::str() const {
  std::string result;
  static constexpr std::size_t gno_str_max_length{
      std::numeric_limits<gno_t>::digits10 + 1U};

  result.reserve(uuid::readable_size + (has_tag() ? tag_.get_size() + 1U : 0U) +
                 1U + gno_str_max_length);
  result += uuid_.str();
  if (has_tag()) {
    result += component_separator;
    result += tag_.get_name();
  }
  result += component_separator;
  result += std::to_string(gno_);
  return result;
}

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
  return output << obj.str();
}

} // namespace binsrv::gtids
