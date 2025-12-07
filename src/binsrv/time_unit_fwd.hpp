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

#ifndef BINSRV_TIME_UNIT_FWD_HPP
#define BINSRV_TIME_UNIT_FWD_HPP

#include <concepts>
#include <istream>
#include <optional>
#include <ostream>

#include "util/nv_tuple_json_support.hpp"

namespace binsrv {

class time_unit;

using optional_time_unit = std::optional<time_unit>;

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_ostream<Char, Traits> &
operator<<(std::basic_ostream<Char, Traits> &output, const time_unit &unit);

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_istream<Char, Traits> &
operator>>(std::basic_istream<Char, Traits> &input, time_unit &unit);

} // namespace binsrv

template <>
struct util::is_string_convertable<binsrv::time_unit> : std::true_type {};

#endif // BINSRV_TIME_UNIT_FWD_HPP
