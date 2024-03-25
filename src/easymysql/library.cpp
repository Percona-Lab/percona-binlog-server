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

#include "easymysql/library.hpp"

#include <cstdint>
#include <stdexcept>
#include <string_view>

#include <mysql/mysql.h>

#include "easymysql/connection.hpp"
#include "easymysql/connection_config_fwd.hpp"

#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace easymysql {

library::library() {
  if (mysql_library_init(0, nullptr, nullptr) != 0) {
    util::exception_location().raise<std::logic_error>(
        "cannot initialize MySQL library");
  }
}

library::~library() { mysql_library_end(); }

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::uint32_t library::get_client_version() const noexcept {
  return util::maybe_useless_integral_cast<std::uint32_t>(
      mysql_get_client_version());
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::string_view library::get_readable_client_version() const noexcept {
  return {mysql_get_client_info()};
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
connection library::create_connection(const connection_config &config) const {
  return connection(config);
}

} // namespace easymysql
