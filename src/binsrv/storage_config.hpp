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

#ifndef BINSRV_STORAGE_CONFIG_HPP
#define BINSRV_STORAGE_CONFIG_HPP

#include "binsrv/storage_config_fwd.hpp" // IWYU pragma: export

#include <string>

#include "binsrv/size_unit.hpp"
#include "binsrv/storage_backend_type_fwd.hpp"
#include "binsrv/time_unit.hpp"

#include "util/common_optional_types.hpp"
#include "util/nv_tuple.hpp"

namespace binsrv {

// clang-format off
struct [[nodiscard]] storage_config
    : util::nv_tuple<
          util::nv<"backend", storage_backend_type>,
          util::nv<"uri", std::string>,
          util::nv<"fs_buffer_directory", util::optional_string>,
          util::nv<"checkpoint_size", optional_size_unit>,
          util::nv<"checkpoint_interval", optional_time_unit>
      > {};
// clang-format on

} // namespace binsrv

#endif // BINSRV_STORAGE_CONFIG_HPP
