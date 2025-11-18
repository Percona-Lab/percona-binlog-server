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

#ifndef UTIL_BOUNDED_STRING_STORAGE_HPP
#define UTIL_BOUNDED_STRING_STORAGE_HPP

#include "util/bounded_string_storage_fwd.hpp" // IWYU pragma: export

#include <string_view>

#include "util/byte_span.hpp"

namespace util {

template <std::size_t N>
std::string_view
to_string_view(const bounded_string_storage<N> &storage) noexcept {
  // in case when every byte of the array is significant (non-zero)
  // we cannot rely on std::string_view::string_view(const char*)
  // constructor as '\0' character will never be found
  auto result{util::as_string_view(storage)};
  auto position{result.find('\0')};
  if (position != std::string_view::npos) {
    result.remove_suffix(std::size(result) - position);
  }
  return result;
}

} // namespace util

#endif // UTIL_BOUNDED_STRING_STORAGE_HPP
