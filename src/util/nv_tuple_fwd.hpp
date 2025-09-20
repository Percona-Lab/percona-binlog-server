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

#ifndef UTIL_NV_TUPLE_FWD_HPP
#define UTIL_NV_TUPLE_FWD_HPP

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "util/ct_string.hpp"

namespace util {

template <ct_string CTS, typename T> struct nv;

template <typename> struct is_named_value : std::false_type {};
template <ct_string CTS, typename T>
struct is_named_value<nv<CTS, T>> : std::true_type {};

template <typename T>
inline constexpr bool is_named_value_v = is_named_value<T>::value;

template <typename T>
concept named_value = is_named_value_v<T>;

template <named_value... NVPack> struct nv_tuple;

template <typename> struct is_named_value_tuple : std::false_type {};
template <named_value... NVPack>
struct is_named_value_tuple<nv_tuple<NVPack...>> : std::true_type {};

template <typename T>
inline constexpr bool is_named_value_tuple_v = is_named_value_tuple<T>::value;

template <typename T>
concept named_value_tuple = is_named_value_tuple_v<T>;

template <named_value... NVPack>
inline const auto &cast_to_nv_tuple(const nv_tuple<NVPack...> &obj) noexcept {
  return obj;
}

template <typename>
struct is_derived_from_named_value_tuple : std::false_type {};

template <typename NVTuple>
  requires requires { cast_to_nv_tuple(std::declval<NVTuple>()); }
struct is_derived_from_named_value_tuple<NVTuple> : std::true_type {};

template <typename NVTuple>
inline constexpr bool is_derived_from_named_value_tuple_v =
    is_derived_from_named_value_tuple<NVTuple>::value;

template <typename NVTuple>
concept derived_from_named_value_tuple =
    is_derived_from_named_value_tuple_v<NVTuple>;

template <typename NVTuple> struct extract_named_value_tuple_base {
  using type =
      std::remove_cvref_t<decltype(cast_to_nv_tuple(std::declval<NVTuple>()))>;
};
template <typename NVTuple>
using extract_named_value_tuple_base_t =
    typename extract_named_value_tuple_base<NVTuple>::type;

template <typename T> struct is_string_convertable : std::false_type {};

template <typename T>
inline constexpr bool is_string_convertable_v = is_string_convertable<T>::value;

template <typename T>
concept string_convertable = is_string_convertable_v<T>;

} // namespace util

#endif // UTIL_NV_TUPLE_FWD_HPP
