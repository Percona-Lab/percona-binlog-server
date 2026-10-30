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

#ifndef UTIL_UUID_HELPERS_HPP
#define UTIL_UUID_HELPERS_HPP

#include <cctype>
#include <cstddef>
#include <string_view>

namespace util {

// Checks the canonical 8-4-4-4-12 hex shape with '-' separators (36 chars),
// matching @@GLOBAL.SERVER_UUID and the output of boost::uuids::to_string.
//
// TODO: it might be cleaner to fold this canonical-UUID validation into the
// first-class binsrv::gtids::uuid type (or share a single definition with it)
// instead of keeping a standalone helper here.
[[nodiscard]] inline bool is_canonical_uuid(std::string_view value) noexcept {
  static constexpr std::size_t uuid_length{36U};
  if (value.size() != uuid_length) {
    return false;
  }
  for (std::size_t index{0U}; index < uuid_length; ++index) {
    const char character{value[index]};
    const bool separator_slot{index == 8U || index == 13U || index == 18U ||
                              index == 23U};
    if (separator_slot) {
      if (character != '-') {
        return false;
      }
    } else if (std::isxdigit(static_cast<unsigned char>(character)) == 0) {
      return false;
    }
  }
  return true;
}

} // namespace util

#endif // UTIL_UUID_HELPERS_HPP
