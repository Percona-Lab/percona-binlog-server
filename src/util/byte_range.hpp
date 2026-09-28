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

#ifndef UTIL_BYTE_RANGE_HPP
#define UTIL_BYTE_RANGE_HPP

#include "util/byte_range_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <string>

#include "util/common_optional_types.hpp"

namespace util {

class byte_range {
public:
  explicit byte_range(std::uint64_t offset = 0ULL,
                      const optional_uint64_t &length = {});

  [[nodiscard]] std::uint64_t get_offset() const noexcept { return offset_; }

  [[nodiscard]] bool has_length() const noexcept { return length_.has_value(); }

  [[nodiscard]] std::uint64_t get_length() const noexcept {
    return length_.value_or(0ULL);
  }

  [[nodiscard]] bool is_empty() const noexcept {
    return length_.has_value() && *length_ == 0ULL;
  }

  [[nodiscard]] bool is_full() const noexcept {
    return offset_ == 0ULL && !length_.has_value();
  }

  [[nodiscard]] std::string to_string() const;

private:
  std::uint64_t offset_;
  optional_uint64_t length_;
};

} // namespace util

#endif // UTIL_BYTE_RANGE_HPP
