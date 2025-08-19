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

#include "util/crc_helpers.hpp"

#include <cstdint>
#include <iterator>

#include <zconf.h>
#include <zlib.h>

#include "util/byte_span_fwd.hpp"

namespace util {

std::uint32_t calculate_crc32(const_byte_span portion) noexcept {
  auto crc = crc32_z(0UL, Z_NULL, 0U);
  return static_cast<std::uint32_t>(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      crc32_z(crc, reinterpret_cast<const Bytef *>(std::data(portion)),
              std::size(portion)));
}

} // namespace util
