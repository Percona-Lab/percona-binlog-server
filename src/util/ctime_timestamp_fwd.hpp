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

#ifndef UTIL_CTIME_TIMESTAMP_FWD_HPP
#define UTIL_CTIME_TIMESTAMP_FWD_HPP

#include <iosfwd>

#include "util/nv_tuple_json_support.hpp"

namespace util {

class ctime_timestamp;

std::ostream &operator<<(std::ostream &output,
                         const ctime_timestamp &timestamp);

std::istream &operator>>(std::istream &input, ctime_timestamp &timestamp);

} // namespace util

template <>
struct util::is_string_convertible<util::ctime_timestamp> : std::true_type {};

#endif // UTIL_CTIME_TIMESTAMP_FWD_HPP
