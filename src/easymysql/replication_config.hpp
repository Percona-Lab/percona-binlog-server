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

#ifndef EASYMYSQL_REPLICATION_CONFIG_HPP
#define EASYMYSQL_REPLICATION_CONFIG_HPP

#include "easymysql/replication_config_fwd.hpp" // IWYU pragma: export

#include <cstdint>

#include "util/nv_tuple.hpp"

namespace easymysql {

struct [[nodiscard]] replication_config
    : util::nv_tuple<
          // clang-format off
          util::nv<"server_id", std::uint32_t>,
          util::nv<"idle_time", std::uint32_t>
          // clang-format on
          > {};

} // namespace easymysql

#endif // EASYMYSQL_REPLICATION_CONFIG_HPP
