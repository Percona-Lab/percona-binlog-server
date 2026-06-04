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

#ifndef UTIL_TIMESTAMP_HELPERS_HPP
#define UTIL_TIMESTAMP_HELPERS_HPP

#include <cstdint>
#include <string>

#include "util/timestamp_types.hpp" // IWYU pragma: export

namespace util {

[[nodiscard]] std::uint64_t high_resolution_time_point_to_microseconds(
    const high_resolution_time_point &timestamp) noexcept;

[[nodiscard]] high_resolution_time_point
microseconds_to_high_resolution_time_point(std::uint64_t microseconds) noexcept;

[[nodiscard]] std::string
microseconds_to_iso_extended_string(std::uint64_t microseconds);

} // namespace util

#endif // UTIL_TIMESTAMP_HELPERS_HPP
