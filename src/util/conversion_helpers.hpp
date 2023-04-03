#ifndef UTIL_CONVERSION_HELPOERS_HPP
#define UTIL_CONVERSION_HELPOERS_HPP

#include <concepts>
#include <type_traits>

namespace util {

template <std::integral To, std::integral From>
[[nodiscard]] To maybe_useless_integral_cast(From value) noexcept {
  if constexpr (std::is_same_v<From, To>) {
    return value;
  } else {
    return static_cast<To>(value);
  }
}

} // namespace util

#endif // UTIL_CONVERSION_HELPOERS_HPP
