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

#ifndef MINIMYSQL_NETWORK_IO_OPERATIONS_FWD_HPP
#define MINIMYSQL_NETWORK_IO_OPERATIONS_FWD_HPP

#include <string>
#include <vector>

namespace minimysql {

using network_buffer_type = std::string;
using network_buffer_container = std::vector<network_buffer_type>;

inline constexpr std::size_t max_payload_size{
    16UZ * 1024UZ * 1024UZ}; // 16 MiB - MySQL's maximum packet size

} // namespace minimysql

#endif // MINIMYSQL_NETWORK_IO_OPERATIONS_FWD_HPP
