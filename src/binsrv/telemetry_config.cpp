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

#include "binsrv/telemetry_config.hpp"

#include <chrono>
#include <string>
#include <string_view>

#include "binsrv/time_unit.hpp"

namespace binsrv {

namespace {

constexpr std::string_view default_path{"/usr/local/percona/telemetry/pbs"};
constexpr std::chrono::seconds default_grace_interval{300};
constexpr std::chrono::seconds default_scrape_interval{60 * 60 * 24};
constexpr std::chrono::seconds default_history_keep_interval{60 * 60 * 24 * 7};

[[nodiscard]] std::chrono::seconds to_seconds(const optional_time_unit &value,
                                              std::chrono::seconds fallback) {
  if (!value.has_value()) {
    return fallback;
  }
  return std::chrono::seconds{
      static_cast<std::chrono::seconds::rep>(value->get_value())};
}

} // anonymous namespace

std::string telemetry_config::path() const {
  const auto &value{get<"path">()};
  return value.has_value() ? *value : std::string{default_path};
}

std::chrono::seconds telemetry_config::grace_interval() const {
  return to_seconds(get<"grace_interval">(), default_grace_interval);
}

std::chrono::seconds telemetry_config::scrape_interval() const {
  return to_seconds(get<"scrape_interval">(), default_scrape_interval);
}

std::chrono::seconds telemetry_config::history_keep_interval() const {
  return to_seconds(get<"history_keep_interval">(),
                    default_history_keep_interval);
}

} // namespace binsrv
