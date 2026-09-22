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

#ifndef BINSRV_STORAGE_CORE_FWD_HPP
#define BINSRV_STORAGE_CORE_FWD_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace binsrv {

enum class storage_construction_mode_type : std::uint8_t {
  querying_only,
  streaming,
  purging
};

enum class open_binlog_status : std::uint8_t {
  created,
  opened_empty,
  opened_at_magic_payload_offset,
  opened_with_data_present
};

struct binlog_encryption_record;
using optional_binlog_encryption_record =
    std::optional<binlog_encryption_record>;

struct binlog_record;
using binlog_record_container = std::vector<binlog_record>;

class storage_core;
using storage_core_ptr = std::unique_ptr<storage_core>;

} // namespace binsrv

#endif // BINSRV_STORAGE_CORE_FWD_HPP
