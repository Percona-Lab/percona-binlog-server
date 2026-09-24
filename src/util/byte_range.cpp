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

#include "util/byte_range.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "util/common_optional_types.hpp"
#include "util/exception_location_helpers.hpp"

namespace util {

byte_range::byte_range(std::uint64_t offset, const optional_uint64_t &length)
    : offset_(offset), length_(length) {
  if (length_.has_value()) {
    if (std::numeric_limits<std::uint64_t>::max() - offset_ < *length_) {
      exception_location().raise<std::overflow_error>(
          "length overflow in byte_range constructor");
    }
  }
}

std::string byte_range::to_string() const {
  if (is_empty()) {
    return "<empty>";
  }
  if (is_full()) {
    return "<full>";
  }
  static constexpr char separator{'-'};

  std::string result{};
  result = std::to_string(offset_);
  result += separator;
  if (length_.has_value()) {
    result += std::to_string(offset_ + *length_ - 1ULL);
  }
  return result;
}

} // namespace util
