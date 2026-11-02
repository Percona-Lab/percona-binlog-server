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

#ifndef BINSRV_TELEMETRY_CONFIG_HPP
#define BINSRV_TELEMETRY_CONFIG_HPP

#include "binsrv/telemetry_config_fwd.hpp" // IWYU pragma: export

#include <chrono>
#include <string>

#include "binsrv/time_unit.hpp"

#include "util/common_optional_types.hpp"
#include "util/nv_tuple.hpp"

namespace binsrv {

// clang-format off
struct [[nodiscard]] telemetry_config
    : util::nv_tuple<
          util::nv<"enabled", util::optional_bool>,
          util::nv<"path", util::optional_string>,
          util::nv<"grace_interval", optional_time_unit>,
          util::nv<"scrape_interval", optional_time_unit>,
          util::nv<"history_keep_interval", optional_time_unit>
      > {
  // All fields are optional; the accessors below apply the defaults.
  [[nodiscard]] bool enabled() const { return get<"enabled">().value_or(true); }
  [[nodiscard]] std::string path() const;
  [[nodiscard]] std::chrono::seconds grace_interval() const;
  [[nodiscard]] std::chrono::seconds scrape_interval() const;
  [[nodiscard]] std::chrono::seconds history_keep_interval() const;
};
// clang-format on

} // namespace binsrv

#endif // BINSRV_TELEMETRY_CONFIG_HPP
