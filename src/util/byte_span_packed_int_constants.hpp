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

#ifndef UTIL_BYTE_SPAN_PACKED_INT_CONSTANTS_HPP
#define UTIL_BYTE_SPAN_PACKED_INT_CONSTANTS_HPP

#include <cstddef>

namespace util {

inline constexpr std::uint64_t packed_int_single_boundary{251ULL};
inline constexpr std::uint64_t packed_int_double_boundary{1ULL << 16U};
inline constexpr std::uint64_t packed_int_triple_boundary{1ULL << 24U};

inline constexpr unsigned char packed_int_max_marker{251U};
inline constexpr unsigned char packed_int_double_marker{252U};
inline constexpr unsigned char packed_int_triple_marker{253U};
inline constexpr unsigned char packed_int_octuple_marker{254U};
inline constexpr unsigned char packed_int_forbidden_marker{255U};

inline constexpr std::size_t packed_int_double_size{2U};
inline constexpr std::size_t packed_int_triple_size{3U};
inline constexpr std::size_t packed_int_octuple_size{8U};

} // namespace util

#endif // UTIL_BYTE_SPAN_PACKED_INT_CONSTANTS_HPP
