// Copyright (c) 2023-2026 Percona and/or its affiliates.
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

#ifndef MINIMYSQL_CONNECTION_CONTEXT_FWD_HPP
#define MINIMYSQL_CONNECTION_CONTEXT_FWD_HPP

#include <bitset>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <boost/describe/enum.hpp>

namespace minimysql {

using connection_buffer_type = std::string;
using connection_buffer_container = std::vector<connection_buffer_type>;

inline constexpr auto number_of_capability_bits{32UZ};
using capability_bitset = std::bitset<number_of_capability_bits>;

// first is column name, second is original column name
using column_name_pair = std::pair<std::string_view, std::string_view>;

// clang-format off
BOOST_DEFINE_FIXED_ENUM_CLASS(client_command_type, std::uint8_t,
  query,
  quit,
  reset_connection,
  change_user,
  ping,
  binlog_dump,
  binlog_dump_gtid,
  unknown)
// clang-format on

class connection_context;

} // namespace minimysql

#endif // MINIMYSQL_CONNECTION_CONTEXT_FWD_HPP
