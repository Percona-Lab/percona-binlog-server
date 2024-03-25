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

#include "easymysql/connection.hpp"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string_view>

#include <mysql/mysql.h>

#include "easymysql/binlog.hpp"
#include "easymysql/connection_config.hpp"
#include "easymysql/connection_deimpl_private.hpp"
#include "easymysql/core_error_helpers_private.hpp"

#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace easymysql {

void connection::connection_deleter::operator()(void *ptr) const noexcept {
  if (ptr != nullptr) {
    mysql_close(static_cast<MYSQL *>(ptr));
  }
}

connection::connection(const connection_config &config)
    : impl_{mysql_init(nullptr)} {
  if (!impl_) {
    util::exception_location().raise<std::logic_error>(
        "cannot create MYSQL object");
  }
  auto *casted_impl = connection_deimpl::get(impl_);
  if (mysql_real_connect(casted_impl,
                         /*        host */ config.get<"host">().c_str(),
                         /*        user */ config.get<"user">().c_str(),
                         /*    password */ config.get<"password">().c_str(),
                         /*          db */ nullptr,
                         /*        port */ config.get<"port">(),
                         /* unix socket */ nullptr,
                         /*       flags */ 0) == nullptr) {
    raise_core_error_from_connection(*this);
  }
}

std::uint32_t connection::get_server_version() const noexcept {
  assert(!is_empty());
  return util::maybe_useless_integral_cast<std::uint32_t>(
      mysql_get_server_version(connection_deimpl::get_const_casted(impl_)));
}

std::string_view connection::get_readable_server_version() const noexcept {
  assert(!is_empty());
  return {mysql_get_server_info(connection_deimpl::get_const_casted(impl_))};
}

std::uint32_t connection::get_protocol_version() const noexcept {
  assert(!is_empty());
  return util::maybe_useless_integral_cast<std::uint32_t>(
      mysql_get_proto_info(connection_deimpl::get_const_casted(impl_)));
}

std::string_view connection::get_server_connection_info() const noexcept {
  assert(!is_empty());
  return {mysql_get_host_info(connection_deimpl::get_const_casted(impl_))};
}

std::string_view connection::get_character_set_name() const noexcept {
  assert(!is_empty());
  return {mysql_character_set_name(connection_deimpl::get_const_casted(impl_))};
}

binlog connection::create_binlog(std::uint32_t server_id,
                                 std::string_view file_name,
                                 std::uint64_t position) {
  assert(!is_empty());
  return {*this, server_id, file_name, position};
}

void connection::execute_generic_query_noresult(std::string_view query) {
  assert(!is_empty());
  auto *casted_impl = connection_deimpl::get(impl_);
  if (mysql_real_query(casted_impl, std::data(query), std::size(query)) != 0) {
    raise_core_error_from_connection(*this);
  }
}

} // namespace easymysql
