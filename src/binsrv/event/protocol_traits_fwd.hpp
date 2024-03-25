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

#ifndef BINSRV_EVENT_PROTOCOL_TRAITS_FWD_HPP
#define BINSRV_EVENT_PROTOCOL_TRAITS_FWD_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace binsrv::event {

inline constexpr std::uint16_t default_binlog_version{4U};
inline constexpr std::size_t default_number_of_events{42U};
inline constexpr std::size_t default_common_header_length{19U};

inline constexpr std::size_t unspecified_post_header_length{
    std::numeric_limits<std::size_t>::max()};

// https://github.com/mysql/mysql-server/blob/trunk/sql/log_event.h#L211
// 4 bytes which all binlogs should begin with
inline constexpr std::array<std::byte, 4> magic_binlog_payload{
    std::byte{0xFE}, std::byte{0x62}, std::byte{0x69}, std::byte{0x6E}};
inline constexpr std::uint64_t magic_binlog_offset{4ULL};

} // namespace binsrv::event

#endif // BINSRV_EVENT_PROTOCOL_TRAITS_FWD_HPP
