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

#ifndef BINSRV_STORAGE_FWD_HPP
#define BINSRV_STORAGE_FWD_HPP

#include <cstdint>

namespace binsrv {

enum class open_binlog_status : std::uint8_t {
  created,
  opened_empty,
  opened_at_magic_paylod_offset,
  opened_with_data_present
};

class storage;

} // namespace binsrv

#endif // BINSRV_STORAGE_FWD_HPP
