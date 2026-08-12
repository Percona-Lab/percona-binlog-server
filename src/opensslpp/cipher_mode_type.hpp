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

#ifndef OPENSSLPP_CIPHER_MODE_TYPE_HPP
#define OPENSSLPP_CIPHER_MODE_TYPE_HPP

#include "opensslpp/cipher_mode_type_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <array>
#include <concepts>
#include <istream>
#include <ostream>
#include <string_view>

#include "util/conversion_helpers.hpp"

namespace opensslpp {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// clang-format off
#define OPENSSLPP_CIPHER_MODE_TYPE_X_SEQUENCE() \
  OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(ecb),  \
  OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(cbc),  \
  OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(cfb),  \
  OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(ofb),  \
  OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(ctr),  \
  OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(gcm),  \
  OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(ccm),  \
  OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(xts),  \
  OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(wrap), \
  OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(ocb),  \
  OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(siv)
// clang-format on

#define OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(X) X
enum class cipher_mode_type : std::uint8_t {
  OPENSSLPP_CIPHER_MODE_TYPE_X_SEQUENCE(),
  delimiter
};
#undef OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO

inline std::string_view to_string_view(cipher_mode_type mode) noexcept {
  using namespace std::string_view_literals;
#define OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO(X) #X##sv
  static constexpr std::array labels{OPENSSLPP_CIPHER_MODE_TYPE_X_SEQUENCE(),
                                     ""sv};
#undef OPENSSLPP_CIPHER_MODE_TYPE_X_MACRO
  const auto index{
      util::enum_to_index(std::min(cipher_mode_type::delimiter, mode))};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  return labels[index];
}
#undef OPENSSLPP_CIPHER_MODE_TYPE_X_SEQUENCE
// NOLINTEND(cppcoreguidelines-macro-usage)

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_ostream<Char, Traits> &
operator<<(std::basic_ostream<Char, Traits> &output, cipher_mode_type mode) {
  return output << to_string_view(mode);
}

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_istream<Char, Traits> &
operator>>(std::basic_istream<Char, Traits> &input, cipher_mode_type &mode) {
  std::string mode_str;
  input >> mode_str;
  if (!input) {
    return input;
  }
  std::size_t index{0U};
  const auto max_index = util::enum_to_index(cipher_mode_type::delimiter);
  while (index < max_index &&
         to_string_view(util::index_to_enum<cipher_mode_type>(index)) !=
             mode_str) {
    ++index;
  }
  if (index < max_index) {
    mode = util::index_to_enum<cipher_mode_type>(index);
  } else {
    input.setstate(std::ios_base::failbit);
  }
  return input;
}

} // namespace opensslpp

#endif // OPENSSLPP_CIPHER_MODE_TYPE_HPP
