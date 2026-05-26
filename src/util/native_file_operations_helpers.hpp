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

#ifndef UTIL_NATIVE_FILE_OPERATIONS_HELPERS_HPP
#define UTIL_NATIVE_FILE_OPERATIONS_HELPERS_HPP

#include <filesystem>

namespace util {

// Forces a previously-completed change to the contents of the
// regular file or directory at 'path' to stable storage so it
// survives a power-loss / hard crash on return.
//
// Raises 'std::runtime_error' if the path cannot be opened, fsync'd,
// or closed.
void fsync(const std::filesystem::path &path);

} // namespace util

#endif // UTIL_NATIVE_FILE_OPERATIONS_HELPERS_HPP
