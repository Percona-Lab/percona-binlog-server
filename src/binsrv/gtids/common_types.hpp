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

#ifndef BINSRV_GTIDS_COMMON_TYPES_HPP
#define BINSRV_GTIDS_COMMON_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <boost/container/container_fwd.hpp>

#include "util/bounded_string_storage_fwd.hpp"

namespace binsrv::gtids {

inline constexpr std::size_t uuid_length{16U};
using uuid_storage = std::array<std::byte, uuid_length>;

inline constexpr std::size_t tag_max_length{32U};
using tag_storage = util::bounded_string_storage<tag_max_length>;

using gno_t = std::uint64_t;
inline constexpr gno_t min_gno{1ULL};
// in MySQL Server code gno_t is a signed type and its max values is defined
// as INT64_MAX
inline constexpr gno_t max_gno{
    std::numeric_limits<std::make_signed_t<gno_t>>::max()};

inline constexpr std::size_t expected_max_gtid_set_length{96U};
using gtid_set_storage =
    boost::container::small_vector<std::byte, expected_max_gtid_set_length>;

} // namespace binsrv::gtids

#endif // BINSRV_GTIDS_COMMON_TYPES_HPP
