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

#ifndef UTIL_COMPOSIT_NAME_FWD_HPP
#define UTIL_COMPOSIT_NAME_FWD_HPP

#include <string>
#include <string_view>

namespace util {

template <typename StringLikeType> class composite_name;

using string_composite_name = composite_name<std::string>;
using string_view_composite_name = composite_name<std::string_view>;
using c_str_composite_name = composite_name<const char *>;

} // namespace util

#endif // UTIL_COMPOSIT_NAME_FWD_HPP
