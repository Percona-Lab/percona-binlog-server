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

#ifndef UTIL_COMMON_OPTIONAL_TYPES_HPP
#define UTIL_COMMON_OPTIONAL_TYPES_HPP

#include <cstdint>
#include <optional>
#include <string>

namespace util {

using optional_string = std::optional<std::string>;

using optional_uint8_t = std::optional<std::uint8_t>;
using optional_uint16_t = std::optional<std::uint16_t>;
using optional_uint32_t = std::optional<std::uint32_t>;
using optional_uint64_t = std::optional<std::uint64_t>;

} // namespace util

#endif // UTIL_COMMON_OPTIONAL_TYPES_HPP
