// Copyright (c) 2026 Percona and/or its affiliates.
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

#ifndef BINSRV_INSTANCE_CONFIG_HPP
#define BINSRV_INSTANCE_CONFIG_HPP

#include "binsrv/instance_config_fwd.hpp" // IWYU pragma: export

#include <string>

#include "util/nv_tuple.hpp"

namespace binsrv {

// clang-format off
struct [[nodiscard]] instance_config
    : util::nv_tuple<
          util::nv<"local_settings_path", std::string>
      > {
  void validate() const;
};
// clang-format on

} // namespace binsrv

#endif // BINSRV_INSTANCE_CONFIG_HPP
