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

#ifndef BINSRV_SIZE_UNIT_HPP
#define BINSRV_SIZE_UNIT_HPP

#include "binsrv/size_unit_fwd.hpp" // IWYU pragma: export

#include <array>
#include <concepts>
#include <cstdint>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace binsrv {

class [[nodiscard]] size_unit {
public:
  size_unit() noexcept : base_value_{0ULL}, shift_index_{0ULL} {}
  explicit size_unit(std::string_view value_sv);

  [[nodiscard]] std::uint64_t get_value() const noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    return base_value_ << shifts[shift_index_].second;
  }

  [[nodiscard]] std::string to_string() const {
    std::string result{std::to_string(base_value_)};
    if (shift_index_ != 0U) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
      result += shifts[shift_index_].first;
    }
    return result;
  }

  [[nodiscard]] std::string get_description() const {
    std::string result{to_string()};
    if (shift_index_ != 0) {
      result += " (";
      result += std::to_string(get_value());
      result += ')';
    }
    return result;
  }

private:
  std::uint64_t base_value_;
  std::size_t shift_index_;

  // clang-format off
  static constexpr std::array shifts{
    std::pair{'_',  0ULL},
    std::pair{'K', 10ULL},
    std::pair{'M', 20ULL},
    std::pair{'G', 30ULL},
    std::pair{'T', 40ULL},
    std::pair{'P', 50ULL}
  };
  // clang-format on
};

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_ostream<Char, Traits> &
operator<<(std::basic_ostream<Char, Traits> &output, const size_unit &unit) {
  return output << unit.to_string();
}

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_istream<Char, Traits> &
operator>>(std::basic_istream<Char, Traits> &input, size_unit &unit) {
  std::string unit_str;
  input >> unit_str;
  if (!input) {
    return input;
  }
  try {
    unit = size_unit{unit_str};
  } catch (const std::exception &) {
    input.setstate(std::ios_base::failbit);
  }
  return input;
}

} // namespace binsrv

#endif // BINSRV_SIZE_UNIT_HPP
