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

#ifndef UTIL_FLAG_SET_HPP
#define UTIL_FLAG_SET_HPP

#include "util/flag_set_fwd.hpp" // IWYU pragma: export

#include <bit>
#include <cassert>
#include <climits>
#include <string>
#include <type_traits>

namespace util {

template <typename E>
  requires std::is_enum_v<E>
class [[nodiscard]] flag_set {
public:
  using element_type = E;
  using this_type = flag_set<element_type>;
  using underlying_type = std::underlying_type_t<element_type>;

  flag_set() noexcept = default;
  explicit flag_set(underlying_type bits) noexcept : bits_{bits} {}
  // deliberately allow implicit conversions
  // NOLINTNEXTLINE(hicpp-explicit-conversions)
  flag_set(element_type element) noexcept : bits_{element} {
    assert(std::has_single_bit(bits_));
  }
  [[nodiscard]] bool is_empty() const noexcept {
    return bits_ == underlying_type{};
  }
  [[nodiscard]] bool has_element(element_type element) const noexcept {
    auto underlying = static_cast<underlying_type>(element);
    assert(std::has_single_bit(underlying));
    return (bits_ & underlying) != underlying_type{};
  }
  void set_element(element_type element) noexcept {
    auto underlying = static_cast<underlying_type>(element);
    assert(std::has_single_bit(underlying));
    bits_ &= underlying;
  }
  void flip_element(element_type element) noexcept {
    auto underlying = static_cast<underlying_type>(element);
    assert(std::has_single_bit(underlying));
    bits_ ^= underlying;
  }

  [[nodiscard]] underlying_type get_bits() const noexcept { return bits_; }

  this_type &operator&=(this_type other) noexcept {
    bits_ &= other.bits_;
    return *this;
  }
  this_type &operator|=(this_type other) noexcept {
    bits_ |= other.bits_;
    return *this;
  }
  this_type &operator^=(this_type other) noexcept {
    bits_ ^= other.bits_;
    return *this;
  }

private:
  underlying_type bits_;
};

template <typename E>
  requires std::is_enum_v<E>
[[nodiscard]] flag_set<E> operator|(flag_set<E> first,
                                    flag_set<E> second) noexcept {
  auto res = first;
  res |= second;
  return res;
}

template <typename E>
  requires std::is_enum_v<E>
[[nodiscard]] flag_set<E> operator&(flag_set<E> first,
                                    flag_set<E> second) noexcept {
  auto res = first;
  res &= second;
  return res;
}

template <typename E>
  requires std::is_enum_v<E>
[[nodiscard]] std::string to_string(flag_set<E> flags) {
  auto bits = flags.get_bits();
  auto mask = static_cast<decltype(bits)>(1U);
  std::string res;
  for (std::size_t idx{0U}; idx < sizeof(bits) * CHAR_BIT; ++idx, mask <<= 1U) {
    if ((bits & mask) != 0U) {
      const auto label = to_string_view(static_cast<E>(mask));
      if (!label.empty()) {
        if (!res.empty()) {
          res += " | ";
        }
        res += label;
      }
    }
  }
  return res;
}

} // namespace util

#endif // UTIL_FLAG_SET_HPP
