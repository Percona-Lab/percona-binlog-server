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

#ifndef UTIL_NV_TUPLE_JSON_SUPPORT_HPP
#define UTIL_NV_TUPLE_JSON_SUPPORT_HPP

#include <type_traits>

namespace util {

template <typename T> struct is_string_convertable : std::false_type {};

template <typename T>
inline constexpr bool is_string_convertable_v = is_string_convertable<T>::value;

template <typename T>
concept string_convertable = is_string_convertable_v<T>;

} // namespace util

#endif // UTIL_NV_TUPLE_JSON_SUPPORT_HPP
