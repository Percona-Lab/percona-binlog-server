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

#ifndef EASYMYSQL_SSL_MODE_TYPE_HPP
#define EASYMYSQL_SSL_MODE_TYPE_HPP

#include "easymysql/ssl_mode_type_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <array>
#include <concepts>
#include <istream>
#include <ostream>
#include <string_view>
#include <type_traits>

#include "util/conversion_helpers.hpp"

namespace easymysql {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// SSL mode codes copied from
// https://github.com/mysql/mysql-server/blob/mysql-8.0.43/include/mysql.h#L271
// https://github.com/mysql/mysql-server/blob/mysql-8.4.6/include/mysql.h#L272
// clang-format off
#define EASYMYSQL_SSL_MODE_TYPE_X_SEQUENCE() \
  EASYMYSQL_SSL_MODE_TYPE_X_MACRO(disabled       ), \
  EASYMYSQL_SSL_MODE_TYPE_X_MACRO(preferred      ), \
  EASYMYSQL_SSL_MODE_TYPE_X_MACRO(required       ), \
  EASYMYSQL_SSL_MODE_TYPE_X_MACRO(verify_ca      ), \
  EASYMYSQL_SSL_MODE_TYPE_X_MACRO(verify_identity)

// clang-format on

#define EASYMYSQL_SSL_MODE_TYPE_X_MACRO(X) X
enum class ssl_mode_type : std::uint8_t {
  EASYMYSQL_SSL_MODE_TYPE_X_SEQUENCE(),
  delimiter
};
#undef EASYMYSQL_SSL_MODE_TYPE_X_MACRO

inline std::string_view to_string_view(ssl_mode_type level) noexcept {
  using namespace std::string_view_literals;
#define EASYMYSQL_SSL_MODE_TYPE_X_MACRO(X) #X##sv
  static constexpr std::array labels{EASYMYSQL_SSL_MODE_TYPE_X_SEQUENCE(),
                                     ""sv};
#undef EASYMYSQL_SSL_MODE_TYPE_X_MACRO
  const auto index{
      util::enum_to_index(std::min(ssl_mode_type::delimiter, level))};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  return labels[index];
}
#undef BINSRV_LOG_SEVERITY_X_SEQUENCE
// NOLINTEND(cppcoreguidelines-macro-usage)

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_ostream<Char, Traits> &
operator<<(std::basic_ostream<Char, Traits> &output, ssl_mode_type level) {
  return output << to_string_view(level);
}

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_istream<Char, Traits> &
operator>>(std::basic_istream<Char, Traits> &input, ssl_mode_type &level) {
  std::string level_str;
  input >> level_str;
  if (!input) {
    return input;
  }
  std::size_t index{0U};
  const auto max_index = util::enum_to_index(ssl_mode_type::delimiter);
  while (index < max_index && to_string_view(util::index_to_enum<ssl_mode_type>(
                                  index)) != level_str) {
    ++index;
  }
  if (index < max_index) {
    level = util::index_to_enum<ssl_mode_type>(index);
  } else {
    input.setstate(std::ios_base::failbit);
  }
  return input;
}

} // namespace easymysql

#endif // EASYMYSQL_SSL_MODE_TYPE_HPP
