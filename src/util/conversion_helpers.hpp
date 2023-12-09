#ifndef UTIL_CONVERSION_HELPOERS_HPP
#define UTIL_CONVERSION_HELPOERS_HPP

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

template <typename EnumType>
  requires(std::is_enum_v<EnumType> &&
           std::is_unsigned_v<std::underlying_type_t<EnumType>>)
[[nodiscard]] constexpr std::size_t enum_to_index(EnumType value) noexcept {
  return static_cast<std::size_t>(
      static_cast<std::underlying_type_t<EnumType>>(value));
}

template <typename EnumType>
  requires(std::is_enum_v<EnumType> &&
           std::is_unsigned_v<std::underlying_type_t<EnumType>>)
[[nodiscard]] constexpr EnumType index_to_enum(std::size_t value) noexcept {
  return static_cast<EnumType>(
      static_cast<std::underlying_type_t<EnumType>>(value));
}

} // namespace util

#endif // UTIL_CONVERSION_HELPOERS_HPP
