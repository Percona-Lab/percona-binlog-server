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
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <limits>

#include <boost/endian/conversion.hpp>

#include "util/byte_span_fwd.hpp"

namespace util {

template <typename T>
  requires std::unsigned_integral<T> || std::same_as<T, std::byte>
void extract_fixed_int_from_byte_span(
    util::const_byte_span &remainder, T &value,
    std::size_t bytes_to_extract = sizeof(T)) noexcept {
  assert(bytes_to_extract != 0U);
  assert(bytes_to_extract <= sizeof(T));
  assert(std::size(remainder) >= bytes_to_extract);
  if constexpr (sizeof(T) != 1U) {
    T value_in_network_format{}; // zero-initialized
    std::memcpy(&value_in_network_format, std::data(remainder),
                bytes_to_extract);
    // A fixed-length unsigned integer stores its value in a series of
    // bytes with the least significant byte first.
    // TODO: in c++23 use std::byteswap()
    value = boost::endian::little_to_native(value_in_network_format);
  } else {
    // TODO: in c++23 change use std::to_underlying(*std::data(remainder))
    using underlying_type = std::underlying_type_t<std::byte>;
    value = static_cast<T>(static_cast<underlying_type>(*std::data(remainder)));
  }
  remainder = remainder.subspan(bytes_to_extract);
}

template <typename T>
  requires std::unsigned_integral<T> || std::same_as<T, std::byte>
[[nodiscard]] bool extract_fixed_int_from_byte_span_checked(
    util::const_byte_span &remainder, T &value,
    std::size_t bytes_to_extract = sizeof(T)) noexcept {
  if (bytes_to_extract > std::size(remainder)) {
    return false;
  }
  extract_fixed_int_from_byte_span(remainder, value, bytes_to_extract);
  return true;
}

template <typename T>
  requires std::signed_integral<T> || std::same_as<T, std::byte>
void extract_fixed_int_from_byte_span(
    util::const_byte_span &remainder, T &value,
    [[maybe_unused]] std::size_t bytes_to_extract = sizeof(T)) noexcept {
  // signed version of the fixed int extractor currently does not support
  // partial byte extraction (when bytes_to_extract != sizeof(T)) because
  // it uses unsigned version underneath
  assert(bytes_to_extract == sizeof(T));
  assert(std::size(remainder) >= bytes_to_extract);
  using unsigned_type = std::make_unsigned_t<T>;
  unsigned_type unsigned_value;
  extract_fixed_int_from_byte_span(remainder, unsigned_value);
  value = static_cast<T>(unsigned_value);
}

template <typename T>
  requires std::signed_integral<T> || std::same_as<T, std::byte>
[[nodiscard]] bool extract_fixed_int_from_byte_span_checked(
    util::const_byte_span &remainder, T &value,
    std::size_t bytes_to_extract = sizeof(T)) noexcept {
  if (bytes_to_extract != sizeof(T) ||
      bytes_to_extract > std::size(remainder)) {
    return false;
  }
  extract_fixed_int_from_byte_span(remainder, value, bytes_to_extract);
  return true;
}

template <typename T>
  requires((std::integral<T> && sizeof(T) == 1U) || std::same_as<T, std::byte>)
void extract_byte_span_from_byte_span(util::const_byte_span &remainder,
                                      std::span<T> storage_span) noexcept {
  assert(std::size(remainder) >= storage_span.size());
  std::memcpy(std::data(storage_span), std::data(remainder),
              storage_span.size());

  remainder = remainder.subspan(storage_span.size());
}

template <typename T>
  requires((std::integral<T> && sizeof(T) == 1U) || std::same_as<T, std::byte>)
[[nodiscard]] bool
extract_byte_span_from_byte_span_checked(util::const_byte_span &remainder,
                                         std::span<T> storage_span) noexcept {
  if (storage_span.size() > std::size(remainder)) {
    return false;
  }
  extract_byte_span_from_byte_span(remainder, storage_span);
  return true;
}

template <typename T, std::size_t N>
  requires((std::integral<T> && sizeof(T) == 1U) || std::same_as<T, std::byte>)
void extract_byte_array_from_byte_span(util::const_byte_span &remainder,
                                       std::array<T, N> &storage) noexcept {
  assert(std::size(remainder) >= N);
  std::memcpy(std::data(storage), std::data(remainder), N);

  remainder = remainder.subspan(N);
}

template <typename T, std::size_t N>
  requires((std::integral<T> && sizeof(T) == 1U) || std::same_as<T, std::byte>)
[[nodiscard]] bool
extract_byte_array_from_byte_span_checked(util::const_byte_span &remainder,
                                          std::array<T, N> &storage) noexcept {
  if (N > std::size(remainder)) {
    return false;
  }
  extract_byte_array_from_byte_span(remainder, storage);
  return true;
}

template <typename T>
  requires(std::unsigned_integral<T> && sizeof(T) == sizeof(std::uint64_t))
[[nodiscard]] bool
extract_packed_int_from_byte_span_checked(util::const_byte_span &remainder,
                                          T &value) noexcept {
  // https://github.com/mysql/mysql-server/blob/mysql-8.0.43/mysys/pack.cc#L90
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/mysys/pack.cc#L90
  util::const_byte_span local_remainder{remainder};
  std::uint8_t first_byte{};
  if (!util::extract_fixed_int_from_byte_span_checked(local_remainder,
                                                      first_byte)) {
    return false;
  }
  static constexpr unsigned char max_marker{251U};
  static constexpr unsigned char double_marker{252U};
  static constexpr unsigned char triple_marker{253U};
  static constexpr unsigned char octuple_marker{254U};
  static constexpr unsigned char forbidden_marker{255U};

  static constexpr std::size_t double_size{2U};
  static constexpr std::size_t triple_size{3U};
  static constexpr std::size_t octuple_size{8U};

  std::uint64_t unpacked{};
  bool result{true};
  switch (first_byte) {
  case max_marker:
    unpacked = std::numeric_limits<std::uint64_t>::max();
    break;
  case double_marker:
    result = util::extract_fixed_int_from_byte_span_checked(
        local_remainder, unpacked, double_size);
    break;
  case triple_marker:
    result = util::extract_fixed_int_from_byte_span_checked(
        local_remainder, unpacked, triple_size);
    break;
  case octuple_marker:
    result = util::extract_fixed_int_from_byte_span_checked(
        local_remainder, unpacked, octuple_size);
    break;
  [[unlikely]] case forbidden_marker:
    result = false;
    break;
  [[likely]] default:
    unpacked = first_byte;
  }
  if (result) {
    remainder = local_remainder;
    value = unpacked;
  }
  return result;
}

template <typename T>
  requires(std::unsigned_integral<T>)
[[nodiscard]] bool
extract_varlen_int_from_byte_span_checked(util::const_byte_span &remainder,
                                          T &value) noexcept {
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/serialization/variable_length_integers.h#L141
  util::const_byte_span local_remainder{remainder};
  std::uint8_t first_byte{};
  if (!util::extract_fixed_int_from_byte_span_checked(local_remainder,
                                                      first_byte)) {
    return false;
  }
  const std::size_t num_bytes{
      static_cast<std::size_t>(std::countr_one(first_byte)) + 1U};
  if (num_bytes > std::size(remainder)) {
    return false;
  }
  T data_cpy{static_cast<T>(first_byte >> num_bytes)};
  if (num_bytes == 1U) {
    value = data_cpy;
    remainder = local_remainder;
    return true;
  }
  std::uint64_t data_tmp{0ULL};
  extract_fixed_int_from_byte_span(local_remainder, data_tmp, num_bytes - 1U);
  // If num_bytes <= 8, shift left by 8 - num_bytes.
  // If num_bytes == 9, shift left by 8 - 9 + 1 = 0.
  if (num_bytes < sizeof(data_tmp)) {
    data_tmp <<= (sizeof(data_tmp) - num_bytes);
  }
  if (data_tmp > std::numeric_limits<T>::max()) {
    return false;
  }
  data_cpy |= static_cast<T>(data_tmp);
  value = data_cpy;
  remainder = local_remainder;
  return true;
}

template <typename T>
  requires(std::signed_integral<T>)
[[nodiscard]] bool
extract_varlen_int_from_byte_span_checked(util::const_byte_span &remainder,
                                          T &value) noexcept {
  using unsigned_type = std::make_unsigned_t<T>;
  unsigned_type data_tmp{};
  const auto result{
      extract_varlen_int_from_byte_span_checked(remainder, data_tmp)};
  if (!result) {
    return false;
  }
  // 0 if positive, ~0 if negative
  const unsigned_type sign_mask{
      (data_tmp & 1U) != 0U ? std::numeric_limits<unsigned_type>::max() : 0U};
  // the result if it is nonnegative, or -(result + 1) if it is negative.
  data_tmp >>= 1U;
  // the result
  data_tmp ^= sign_mask;
  value = static_cast<T>(data_tmp);
  return true;
}

} // namespace util

#endif // UTIL_BYTE_SPAN_EXTRACTORS_HPP
