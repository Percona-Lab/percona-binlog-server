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

#ifndef UTIL_BYTE_SPAN_EXTRACTORS_HPP
#define UTIL_BYTE_SPAN_EXTRACTORS_HPP

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>

#include <boost/endian/conversion.hpp>

#include "util/byte_span_fwd.hpp"

namespace util {

template <typename T>
  requires std::integral<T> || std::same_as<T, std::byte>
void extract_fixed_int_from_byte_span(util::const_byte_span &remainder,
                                      T &value) noexcept {
  assert(std::size(remainder) >= sizeof value);
  T value_in_network_format;
  std::memcpy(&value_in_network_format, std::data(remainder), sizeof(T));
  // A fixed-length unsigned integer stores its value in a series of
  // bytes with the least significant byte first.
  // TODO: in c++23 use std::byteswap()
  value = boost::endian::little_to_native(value_in_network_format);
  remainder = remainder.subspan(sizeof(T));
}

template <typename T>
  requires(std::integral<T> || std::same_as<T, std::byte>) && (sizeof(T) == 1U)
void extract_byte_span_from_byte_span(util::const_byte_span &remainder,
                                      std::span<T> storage_span) noexcept {
  assert(std::size(remainder) >= storage_span.size());
  std::memcpy(std::data(storage_span), std::data(remainder),
              storage_span.size());

  remainder = remainder.subspan(storage_span.size());
}

template <typename T, std::size_t N>
  requires(std::integral<T> || std::same_as<T, std::byte>) && (sizeof(T) == 1U)
void extract_byte_array_from_byte_span(util::const_byte_span &remainder,
                                       std::array<T, N> &storage) noexcept {
  assert(std::size(remainder) >= N);
  std::memcpy(std::data(storage), std::data(remainder), N);

  remainder = remainder.subspan(N);
}

} // namespace util

#endif // UTIL_BYTE_SPAN_EXTRACTORS_HPP
