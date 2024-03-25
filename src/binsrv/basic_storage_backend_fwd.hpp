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

#ifndef BINSRV_BASIC_STORAGE_BACKEND_FWD_HPP
#define BINSRV_BASIC_STORAGE_BACKEND_FWD_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace binsrv {

class basic_storage_backend;

using basic_storage_backend_ptr = std::unique_ptr<basic_storage_backend>;

// the following hash calculation helper allows to look for keys represented
// by either const char*, std::string_view of const std::string in unordered
// containers with std::string key transparently without converting the value
// being searched to std::string
struct transparent_string_like_hash {
  using is_transparent = void;
  std::size_t operator()(const char *key) const noexcept {
    return std::hash<std::string_view>{}(key);
  }
  std::size_t operator()(std::string_view key) const noexcept {
    return std::hash<std::string_view>{}(key);
  }
  std::size_t operator()(const std::string &key) const noexcept {
    return std::hash<std::string>{}(key);
  }
};
// the container thatr uses transparent_string_like_hash also needs a
// transparent version of KeyEqual template argument (std::equal_to<void>)
using storage_object_name_container =
    std::unordered_map<std::string, std::uint64_t, transparent_string_like_hash,
                       std::equal_to<>>;
} // namespace binsrv

#endif // BINSRV_BASIC_STORAGE_BACKEND_FWD_HPP
