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

#ifndef UTIL_FILE_OPERATIONS_HELPERS_HPP
#define UTIL_FILE_OPERATIONS_HELPERS_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "util/byte_range.hpp"
#include "util/common_optional_types.hpp"

namespace util {

// reads exactly 'length' bytes starting at 'offset'
// if length is not specified (nullopt), reads until the end of the file
// raises if the file is shorter than 'offset + length'
// raises if actual length (either specified by the 'length' parameter
// or determined by reading until the end of the file) is more than
// 'max_length'
[[nodiscard]] std::string
read_file_content(std::string_view error_label,
                  const std::filesystem::path &path, std::size_t max_size,
                  const byte_range &range = byte_range{});

void write_file_content(std::string_view error_label,
                        const std::filesystem::path &path,
                        std::string_view content);

} // namespace util

#endif // UTIL_FILE_OPERATIONS_HELPERS_HPP
