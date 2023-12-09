#ifndef UTIL_CT_STRING_HPP
#define UTIL_CT_STRING_HPP

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace util {

// in order for ct_string to be a non-type template argument, all its non-static
// members must be public
// for convenience, the instances of this class should be implicitly created
// from the string literals (const char(&)[])
template <std::size_t N> struct ct_string {
  // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
  // NOLINTNEXTLINE(hicpp-explicit-conversions)
  constexpr ct_string(const char (&str)[N]) noexcept {
    std::copy_n(std::cbegin(str), N, std::begin(data));
  }
  // NOLINTEND(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)

  [[nodiscard]] constexpr const char *c_str() const noexcept {
    return std::cbegin(data);
  }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return N - 1U; }
  [[nodiscard]] constexpr std::string_view sv() const noexcept {
    return {std::cbegin(data), N - 1U};
  }
  // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
  char data[N] = {}; // NOLINT(misc-non-private-member-variables-in-classes)
  // NOLINTEND(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
};

template <std::size_t N1, std::size_t N2>
constexpr inline auto operator==(const ct_string<N1> &cts1,
                                 const ct_string<N2> &cts2) noexcept {
  return cts1.sv() == cts2.sv();
}
template <std::size_t N1, std::size_t N2>
constexpr inline auto operator<=>(const ct_string<N1> &cts1,
                                  const ct_string<N2> &cts2) noexcept {
  return cts1.sv() <=> cts2.sv();
}

} // namespace util

#endif // UTIL_CT_STRING_HPP
