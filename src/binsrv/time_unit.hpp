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

#ifndef BINSRV_TIME_UNIT_HPP
#define BINSRV_TIME_UNIT_HPP

#include "binsrv/time_unit_fwd.hpp" // IWYU pragma: export

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace binsrv {

class [[nodiscard]] time_unit {
public:
  time_unit() noexcept : base_value_{0ULL}, multiplier_index_{0ULL} {}
  explicit time_unit(std::string_view value_sv);

  [[nodiscard]] std::uint64_t get_value() const noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    return base_value_ * multipliers[multiplier_index_].second;
  }

  [[nodiscard]] std::string to_string() const {
    std::string result{std::to_string(base_value_)};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    result += multipliers[multiplier_index_].first;
    return result;
  }

  [[nodiscard]] std::string get_description() const {
    std::string result{to_string()};
    if (multiplier_index_ != 0) {
      result += " (";
      result += std::to_string(get_value());
      result += multipliers.front().first;
      result += ')';
    }
    return result;
  }

private:
  std::uint64_t base_value_;
  std::size_t multiplier_index_;

  // clang-format off
  static constexpr std::array multipliers{
    std::pair{'s',  1ULL},
    std::pair{'m', 60ULL},
    std::pair{'h', 60ULL * 60ULL},
    std::pair{'d', 60ULL * 60ULL * 24ULL}
  };
  // clang-format on
};

} // namespace binsrv

#endif // BINSRV_TIME_UNIT_HPP
