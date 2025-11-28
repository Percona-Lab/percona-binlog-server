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

#include "binsrv/gtids/uuid.hpp"

#include <algorithm>
#include <exception>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "binsrv/gtids/common_types.hpp"

#include "util/exception_location_helpers.hpp"

namespace binsrv::gtids {

uuid::uuid(std::string_view value) {
  try {
    data_ =
        boost::uuids::string_generator{}(std::cbegin(value), std::cend(value));
  } catch (const std::exception &) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot create uuid");
  }
}

uuid::uuid(const uuid_storage &data) {
  static_assert(std::tuple_size_v<uuid_storage> ==
                boost::uuids::uuid::static_size());
  std::copy_n(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<const boost::uuids::uuid::value_type *>(std::data(data)),
      boost::uuids::uuid::static_size(), std::begin(data_));
}

[[nodiscard]] std::string uuid::str() const {
  return boost::uuids::to_string(data_);
}

std::ostream &operator<<(std::ostream &output, const uuid &obj) {
  return output << obj.str();
}

} // namespace binsrv::gtids
