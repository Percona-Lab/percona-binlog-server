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

#ifndef UTIL_CONVERSION_HELPERS_HPP
#define UTIL_CONVERSION_HELPERS_HPP

#include <concepts>
#include <type_traits>

namespace util {

template <std::integral To, std::integral From>
[[nodiscard]] constexpr To maybe_useless_integral_cast(From value) noexcept {
  if constexpr (std::is_same_v<From, To>) {
    return value;
  } else {
    return static_cast<To>(value);
  }
}

// TODO: remove this function when switch to c++23 and use std::to_underlying
//       instead
template <typename EnumType>
  requires std::is_enum_v<EnumType>
[[nodiscard]] constexpr std::underlying_type_t<EnumType>
to_underlying(EnumType enum_value) noexcept {
  return static_cast<std::underlying_type_t<EnumType>>(enum_value);
}

template <typename EnumType>
  requires std::is_enum_v<EnumType>
[[nodiscard]] constexpr EnumType
from_underlying(std::underlying_type_t<EnumType> value) noexcept {
  return static_cast<EnumType>(value);
}

template <typename EnumType>
  requires(std::is_enum_v<EnumType> &&
           std::is_unsigned_v<std::underlying_type_t<EnumType>> &&
           sizeof(std::underlying_type_t<EnumType>) <= sizeof(std::size_t))
[[nodiscard]] constexpr std::size_t
enum_to_index(EnumType enum_value) noexcept {
  return static_cast<std::size_t>(to_underlying(enum_value));
}

template <typename EnumType>
  requires(std::is_enum_v<EnumType> &&
           std::is_unsigned_v<std::underlying_type_t<EnumType>> &&
           sizeof(std::underlying_type_t<EnumType>) <= sizeof(std::size_t))
[[nodiscard]] constexpr EnumType index_to_enum(std::size_t value) noexcept {
  return from_underlying<EnumType>(
      static_cast<std::underlying_type_t<EnumType>>(value));
}

template <std::integral Type>
[[nodiscard]] constexpr std::make_unsigned_t<Type>
to_unsigned(Type value) noexcept {
  return static_cast<std::make_unsigned_t<Type>>(value);
}

template <std::integral Type>
[[nodiscard]] constexpr std::make_signed_t<Type>
to_signed(Type value) noexcept {
  return static_cast<std::make_signed_t<Type>>(value);
}

} // namespace util

#endif // UTIL_CONVERSION_HELPERS_HPP
