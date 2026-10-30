// Copyright (c) 2026 Percona and/or its affiliates.
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

#ifndef BINSRV_INSTANCE_ID_HPP
#define BINSRV_INSTANCE_ID_HPP

#include <filesystem>
#include <string>

namespace binsrv {

// Returns this instance's stable identifier as a canonical lowercase
// 8-4-4-4-12 hex UUID.
//
// The value is persisted in the file "instance_id" inside
// 'local_settings_path'. When that file already holds a canonical UUID it is
// returned unchanged; otherwise (file absent, unreadable, or holding
// something that is not a canonical UUID) a new UUID v4 is generated,
// written atomically to that file, and returned. 'local_settings_path' is
// expected to be an existing, writable directory (guaranteed by
// instance_config validation).
//
// Raises 'std::runtime_error' if a freshly generated id cannot be persisted.
[[nodiscard]] std::string
load_or_create_instance_id(const std::filesystem::path &local_settings_path);

} // namespace binsrv

#endif // BINSRV_INSTANCE_ID_HPP
