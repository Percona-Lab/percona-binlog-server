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

#include "binsrv/events/rotate_body_impl.hpp"

#include <algorithm>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <string_view>

#include "binsrv/composite_binlog_name.hpp"

#include "binsrv/events/code_type.hpp"

#include "util/byte_span.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::events {

generic_body_impl<code_type::rotate>::generic_body_impl(
    const composite_binlog_name &binlog_name)
    : binlog_{} {
  const auto binlog_name_s{binlog_name.str()};
  const auto binlog_name_span{util::as_const_byte_span(binlog_name_s)};

  binlog_.assign(std::cbegin(binlog_name_span), std::cend(binlog_name_span));
}

generic_body_impl<code_type::rotate>::generic_body_impl(
    util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  // no need to check if member reordering is OK as this class has
  // only one member for holding data of varying length

  binlog_.assign(std::cbegin(portion), std::cend(portion));
}

[[nodiscard]] composite_binlog_name
generic_body_impl<code_type::rotate>::get_parsed_binlog() const {
  return composite_binlog_name::parse(get_readable_binlog());
}

void generic_body_impl<code_type::rotate>::encode_to(
    util::byte_span &destination) const {
  if (std::size(destination) < calculate_encoded_size()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot encode rotate event body");
  }
  std::ranges::copy(binlog_, std::begin(destination));
  destination = destination.subspan(calculate_encoded_size());
}

std::ostream &operator<<(std::ostream &output,
                         const generic_body_impl<code_type::rotate> &obj) {
  return output << "binlog: " << obj.get_readable_binlog();
}

} // namespace binsrv::events
