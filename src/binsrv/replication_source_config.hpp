// Copyright (c) 2023-2026 Percona and/or its affiliates.
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

#ifndef BINSRV_REPLICATION_SOURCE_CONFIG_HPP
#define BINSRV_REPLICATION_SOURCE_CONFIG_HPP

#include "binsrv/replication_source_config_fwd.hpp" // IWYU pragma: export

#include <cstdint>

#include "util/nv_tuple.hpp"

namespace binsrv {

struct [[nodiscard]] replication_source_config
    : util::nv_tuple<
          // clang-format off
          util::nv<"port"         , std::uint16_t>,
          util::nv<"read_timeout" , std::uint32_t>,
          util::nv<"write_timeout", std::uint32_t>
          // clang-format on
          > {
  void validate() const;
};

} // namespace binsrv

#endif // BINSRV_REPLICATION_SOURCE_CONFIG_HPP
