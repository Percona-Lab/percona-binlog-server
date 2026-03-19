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

#ifndef EASYMYSQL_SSL_MODE_TYPE_FWD_HPP
#define EASYMYSQL_SSL_MODE_TYPE_FWD_HPP

#include <concepts>
#include <cstdint>
#include <iosfwd>

#include "util/nv_tuple_json_support.hpp"

namespace easymysql {

// NOLINTNEXTLINE(readability-enum-initial-value,cert-int09-c)
enum class ssl_mode_type : std::uint8_t;

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_ostream<Char, Traits> &
operator<<(std::basic_ostream<Char, Traits> &output, ssl_mode_type level);

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_istream<Char, Traits> &
operator>>(std::basic_istream<Char, Traits> &input, ssl_mode_type &level);

} // namespace easymysql

template <>
struct util::is_string_convertible<easymysql::ssl_mode_type> : std::true_type {
};

#endif // EASYMYSQL_SSL_MODE_TYPE_FWD_HPP
