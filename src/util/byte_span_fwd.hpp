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

#ifndef UTIL_BYTE_SPAN_FWD_HPP
#define UTIL_BYTE_SPAN_FWD_HPP

#include <cstddef>
#include <span>

namespace util {

using byte_span = std::span<std::byte>;
using const_byte_span = std::span<const std::byte>;

} // namespace util

#endif // UTIL_BYTE_SPAN_FWD_HPP
