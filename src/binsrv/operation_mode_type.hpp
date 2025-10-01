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

#ifndef BINSRV_OPERATION_MODE_TYPE_HPP
#define BINSRV_OPERATION_MODE_TYPE_HPP

#include "binsrv/operation_mode_type_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <array>
#include <istream>
#include <ostream>
#include <string_view>
#include <type_traits>

#include "util/conversion_helpers.hpp"

namespace binsrv {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// clang-format off
#define BINSRV_OPERATION_MODE_TYPE_X_SEQUENCE() \
  BINSRV_OPERATION_MODE_TYPE_X_MACRO(fetch  ),  \
  BINSRV_OPERATION_MODE_TYPE_X_MACRO(pull   ),  \
  BINSRV_OPERATION_MODE_TYPE_X_MACRO(version)
// clang-format on

#define BINSRV_OPERATION_MODE_TYPE_X_MACRO(X) X
enum class operation_mode_type : std::uint8_t {
  BINSRV_OPERATION_MODE_TYPE_X_SEQUENCE(),
  delimiter
};
#undef BINSRV_OPERATION_MODE_TYPE_X_MACRO

inline std::string_view
to_string_view(operation_mode_type operation_mode) noexcept {
  using namespace std::string_view_literals;
#define BINSRV_OPERATION_MODE_TYPE_X_MACRO(X) #X##sv
  static constexpr std::array labels{BINSRV_OPERATION_MODE_TYPE_X_SEQUENCE(),
                                     ""sv};
#undef BINSRV_OPERATION_MODE_TYPE_X_MACRO
  const auto index{util::enum_to_index(
      std::min(operation_mode_type::delimiter, operation_mode))};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  return labels[index];
}
#undef BINSRV_OPERATION_MODE_TYPE_X_SEQUENCE
// NOLINTEND(cppcoreguidelines-macro-usage)

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_ostream<Char, Traits> &
operator<<(std::basic_ostream<Char, Traits> &output,
           operation_mode_type operation_mode) {
  return output << to_string_view(operation_mode);
}

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_istream<Char, Traits> &
operator>>(std::basic_istream<Char, Traits> &input,
           operation_mode_type &operation_mode) {
  std::string operation_mode_str;
  input >> operation_mode_str;
  if (!input) {
    return input;
  }
  std::size_t index{0U};
  const auto max_index = util::enum_to_index(operation_mode_type::delimiter);
  while (index < max_index &&
         to_string_view(util::index_to_enum<operation_mode_type>(index)) !=
             operation_mode_str) {
    ++index;
  }
  if (index < max_index) {
    operation_mode = util::index_to_enum<operation_mode_type>(index);
  } else {
    input.setstate(std::ios_base::failbit);
  }
  return input;
}

} // namespace binsrv

#endif // BINSRV_OPERATION_MODE_TYPE_HPP
