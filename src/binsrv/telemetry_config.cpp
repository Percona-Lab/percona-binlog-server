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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "binsrv/time_unit.hpp"

namespace binsrv {

namespace {

constexpr std::string_view default_path{"/usr/local/percona/telemetry/pbs"};
constexpr std::chrono::seconds default_grace_interval{300};
constexpr std::chrono::seconds default_scrape_interval{60 * 60 * 24};
constexpr std::chrono::seconds default_history_keep_interval{60 * 60 * 24 * 7};

// Permitted range for each interval.
// A configured value outside its range is clamped to the nearest bound.
struct interval_range {
  std::chrono::seconds min;
  std::chrono::seconds max;
};

constexpr interval_range scrape_interval_range{
    .min = std::chrono::seconds{10},
    .max = std::chrono::seconds{60 * 60 * 24 * 7}};
constexpr interval_range grace_interval_range{
    .min = std::chrono::seconds{20},
    .max = std::chrono::seconds{60 * 60 * 24 * 2}};
constexpr interval_range history_keep_interval_range{
    .min = std::chrono::seconds{60},
    .max = std::chrono::seconds{60 * 60 * 24 * 7}};

// Clamp a raw configured value (in seconds) into the range.
//
// The clamp is deliberately done in the unsigned domain rather than on
// std::chrono::seconds. time_unit::get_value() returns a std::uint64_t, and it
// can exceed the std::chrono::seconds representation (a signed 64-bit integer):
// time_unit's constructor only rejects the multiplication base_value * unit
// multiplier overflowing 64 bits, so a plain-seconds value (unit multiplier 1,
// e.g. "18000000000000000000s") passes that check yet is larger than INT64_MAX.
// Converting such a value to std::chrono::seconds before clamping would wrap it
// to a negative number, which std::clamp would then pin to min -- the exact
// opposite of the intended max. Clamping the raw uint64 first guarantees any
// out-of-range value collapses onto max, and the result (always within range)
// is safe to narrow to std::chrono::seconds afterwards.
[[nodiscard]] std::uint64_t clamp_to_range(std::uint64_t raw,
                                           const interval_range &range) {
  return std::clamp(raw, static_cast<std::uint64_t>(range.min.count()),
                    static_cast<std::uint64_t>(range.max.count()));
}

// Resolve a configured interval into seconds: the fallback when unset (the
// fallbacks all lie within their ranges), otherwise the configured value
// clamped into the range.
[[nodiscard]] std::chrono::seconds
resolve_interval(const optional_time_unit &value, std::chrono::seconds fallback,
                 const interval_range &range) {
  if (!value.has_value()) {
    return fallback;
  }
  return std::chrono::seconds{static_cast<std::chrono::seconds::rep>(
      clamp_to_range(value->get_value(), range))};
}

} // anonymous namespace

std::string telemetry_config::path() const {
  const auto &value{get<"path">()};
  return value.has_value() ? *value : std::string{default_path};
}

std::chrono::seconds telemetry_config::grace_interval() const {
  return resolve_interval(get<"grace_interval">(), default_grace_interval,
                          grace_interval_range);
}

std::chrono::seconds telemetry_config::scrape_interval() const {
  return resolve_interval(get<"scrape_interval">(), default_scrape_interval,
                          scrape_interval_range);
}

std::chrono::seconds telemetry_config::history_keep_interval() const {
  return resolve_interval(get<"history_keep_interval">(),
                          default_history_keep_interval,
                          history_keep_interval_range);
}

} // namespace binsrv
