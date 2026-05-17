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

#ifndef UTIL_TIMESTAMP_TYPES_HPP
#define UTIL_TIMESTAMP_TYPES_HPP

#include <chrono>
#include <optional>

namespace util {

using high_resolution_time_point =
    std::chrono::high_resolution_clock::time_point;
using optional_high_resolution_time_point =
    std::optional<high_resolution_time_point>;

} // namespace util

#endif // UTIL_TIMESTAMP_TYPES_HPP
