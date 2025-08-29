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

#ifndef EASYMYSQL_SSL_CONFIG_HPP
#define EASYMYSQL_SSL_CONFIG_HPP

#include "easymysql/ssl_config_fwd.hpp" // IWYU pragma: export

#include <cstdint>

#include "easymysql/config_common_types.hpp"
#include "easymysql/ssl_mode_type.hpp"

#include "util/nv_tuple.hpp"

namespace easymysql {

// clang-format off
struct [[nodiscard]] ssl_config
    : util::nv_tuple<
          util::nv<"mode"   , ssl_mode_type>,
          util::nv<"ca"     , optional_string>,
          util::nv<"capath" , optional_string>,
          util::nv<"crl"    , optional_string>,
          util::nv<"crlpath", optional_string>,
          util::nv<"cert"   , optional_string>,
          util::nv<"key"    , optional_string>,
          util::nv<"cipher" , optional_string>
          > {};
// clang-format on

} // namespace easymysql

#endif // EASYMYSQL_SSL_CONFIG_HPP
