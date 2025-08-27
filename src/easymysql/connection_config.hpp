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

#ifndef EASYMYSQL_CONNECTION_CONFIG_HPP
#define EASYMYSQL_CONNECTION_CONFIG_HPP

#include "easymysql/connection_config_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <string>

#include "easymysql/ssl_config.hpp" // IWYU pragma: export
#include "easymysql/tls_config.hpp" // IWYU pragma: export

#include "util/nv_tuple.hpp"

namespace easymysql {

struct [[nodiscard]] connection_config
    : util::nv_tuple<
          // clang-format off
          util::nv<"host"           , std::string>,
          util::nv<"port"           , std::uint16_t>,
          util::nv<"user"           , std::string>,
          util::nv<"password"       , std::string>,
          util::nv<"connect_timeout", std::uint32_t>,
          util::nv<"read_timeout"   , std::uint32_t>,
          util::nv<"write_timeout"  , std::uint32_t>,
          util::nv<"ssl"            , optional_ssl_config>,
          util::nv<"tls"            , optional_tls_config>
          // clang-format on
          > {
  [[nodiscard]] bool has_password() const noexcept {
    return !get<"password">().empty();
  }

  [[nodiscard]] std::string get_connection_string() const;
};

} // namespace easymysql

#endif // EASYMYSQL_CONNECTION_CONFIG_HPP
