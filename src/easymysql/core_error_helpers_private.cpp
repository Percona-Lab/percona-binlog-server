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

#include "easymysql/core_error_helpers_private.hpp"

#include <source_location>

#include <mysql/mysql.h>

#include "easymysql/connection.hpp"
#include "easymysql/connection_deimpl_private.hpp"
#include "easymysql/core_error.hpp"

#include "util/exception_location_helpers.hpp"

namespace easymysql {

struct raise_access {
  static const connection::impl_ptr &get(const connection &conn) noexcept {
    return conn.impl_;
  }
};

[[noreturn]] void
raise_core_error_from_connection(const connection &conn,
                                 std::source_location location) {
  // default value std::source_location::current() for the 'location'
  // parameter is specified in the declaration of this function
  // and passed to the util::exception_location's constructor
  // instead of calling util::exception_location's default constructor
  // because otherwise the location will always point to this
  // particular line on this particular file regardless of the actual
  // location from where this function is called
  auto *casted_impl =
      connection_deimpl::get_const_casted(raise_access::get(conn));
  util::exception_location(location).raise<core_error>(
      static_cast<int>(mysql_errno(casted_impl)), mysql_error(casted_impl));
}

} // namespace easymysql
