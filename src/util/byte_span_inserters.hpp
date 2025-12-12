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

#ifndef UTIL_BYTE_SPAN_INSERTERS_HPP
#define UTIL_BYTE_SPAN_INSERTERS_HPP

#include <array>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <limits>

#include <boost/endian/conversion.hpp>

#include "util/byte_span_fwd.hpp"
#include "util/byte_span_packed_int_constants.hpp"

namespace util {

template <typename T>
  requires std::unsigned_integral<T> || std::same_as<T, std::byte>
void insert_fixed_int_to_byte_span(
    byte_span &remainder, T value,
    std::size_t bytes_to_insert = sizeof(T)) noexcept {
  assert(bytes_to_insert != 0U);
  assert(bytes_to_insert <= sizeof(T));
  assert(std::size(remainder) >= bytes_to_insert);
  if constexpr (sizeof(T) != 1U) {
    // A fixed-length unsigned integer stores its value in a series of
    // bytes with the least significant byte first.
    // TODO: in c++23 use std::byteswap()
    T value_in_network_format{boost::endian::native_to_little(value)};
    std::memcpy(std::data(remainder), &value_in_network_format,
                bytes_to_insert);
  } else {
    using underlying_type = std::underlying_type_t<std::byte>;
    *std::data(remainder) =
        static_cast<std::byte>(static_cast<underlying_type>(value));
  }
  remainder = remainder.subspan(bytes_to_insert);
}

template <typename T>
  requires std::unsigned_integral<T> || std::same_as<T, std::byte>
[[nodiscard]] bool insert_fixed_int_to_byte_span_checked(
    byte_span &remainder, T value,
    std::size_t bytes_to_insert = sizeof(T)) noexcept {
  if (bytes_to_insert > std::size(remainder)) {
    return false;
  }
  insert_fixed_int_to_byte_span(remainder, value, bytes_to_insert);
  return true;
}

template <typename T>
  requires std::signed_integral<T> || std::same_as<T, std::byte>
void insert_fixed_int_to_byte_span(
    byte_span &remainder, T value,
    [[maybe_unused]] std::size_t bytes_to_insert = sizeof(T)) noexcept {
  // signed version of the fixed int inserter currently does not support
  // partial byte insertion (when bytes_to_insert != sizeof(T)) because
  // it uses unsigned version underneath
  assert(bytes_to_insert == sizeof(T));
  assert(std::size(remainder) >= bytes_to_insert);
  using unsigned_type = std::make_unsigned_t<T>;
  const auto unsigned_value{static_cast<unsigned_type>(value)};
  insert_fixed_int_to_byte_span(remainder, unsigned_value);
}

template <typename T>
  requires std::signed_integral<T> || std::same_as<T, std::byte>
[[nodiscard]] bool insert_fixed_int_to_byte_span_checked(
    byte_span &remainder, T value,
    std::size_t bytes_to_insert = sizeof(T)) noexcept {
  if (bytes_to_insert != sizeof(T) || bytes_to_insert > std::size(remainder)) {
    return false;
  }
  insert_fixed_int_to_byte_span(remainder, value, bytes_to_insert);
  return true;
}

template <typename T>
  requires((std::integral<T> && sizeof(T) == 1U) || std::same_as<T, std::byte>)
void insert_byte_span_to_byte_span(byte_span &remainder,
                                   std::span<const T> storage_span) noexcept {
  assert(std::size(remainder) >= std::size(storage_span));
  std::memcpy(std::data(remainder), std::data(storage_span),
              std::size(storage_span));

  remainder = remainder.subspan(std::size(storage_span));
}

template <typename T>
  requires((std::integral<T> && sizeof(T) == 1U) || std::same_as<T, std::byte>)
[[nodiscard]] bool insert_byte_span_to_byte_span_checked(
    byte_span &remainder, std::span<const T> storage_span) noexcept {
  if (std::size(storage_span) > std::size(remainder)) {
    return false;
  }
  insert_byte_span_to_byte_span(remainder, storage_span);
  return true;
}

template <typename T, std::size_t N>
  requires((std::integral<T> && sizeof(T) == 1U) || std::same_as<T, std::byte>)
void insert_byte_array_to_byte_span(byte_span &remainder,
                                    const std::array<T, N> &storage) noexcept {
  assert(std::size(remainder) >= N);
  std::memcpy(std::data(remainder), std::data(storage), N);

  remainder = remainder.subspan(N);
}

template <typename T, std::size_t N>
  requires((std::integral<T> && sizeof(T) == 1U) || std::same_as<T, std::byte>)
[[nodiscard]] bool insert_byte_array_to_byte_span_checked(
    byte_span &remainder, const std::array<T, N> &storage) noexcept {
  if (N > std::size(remainder)) {
    return false;
  }
  insert_byte_array_to_byte_span(remainder, storage);
  return true;
}

template <typename T>
  requires(std::unsigned_integral<T> && sizeof(T) == sizeof(std::uint64_t))
[[nodiscard]] std::size_t calculate_packed_int_size(T value) noexcept {
  // https://github.com/mysql/mysql-server/blob/mysql-8.0.43/mysys/pack.cc#L161
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/mysys/pack.cc#L161
  if (value < packed_int_single_boundary) {
    return 1U;
  }
  if (value < packed_int_double_boundary) {
    return packed_int_double_size + 1U;
  }
  if (value < packed_int_triple_boundary) {
    return packed_int_triple_size + 1U;
  }
  return packed_int_octuple_size + 1U;
}

template <typename T>
  requires(std::unsigned_integral<T> && sizeof(T) == sizeof(std::uint64_t))
[[nodiscard]] bool insert_packed_int_to_byte_span_checked(byte_span &remainder,
                                                          T value) noexcept {
  // https://github.com/mysql/mysql-server/blob/mysql-8.0.43/mysys/pack.cc#L129
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/mysys/pack.cc#L129

  if (value < packed_int_single_boundary) {
    if (std::size(remainder) < 1U) {
      return false;
    }
    insert_fixed_int_to_byte_span(remainder, value, 1);
  }
  /* 251 is reserved for NULL */
  if (value < packed_int_double_boundary) {
    if (std::size(remainder) < 1U + packed_int_double_size) {
      return false;
    }
    insert_fixed_int_to_byte_span(remainder, packed_int_double_marker);
    insert_fixed_int_to_byte_span(remainder, value, packed_int_double_size);
  }
  if (value < packed_int_triple_boundary) {
    if (std::size(remainder) < 1U + packed_int_triple_size) {
      return false;
    }
    insert_fixed_int_to_byte_span(remainder, packed_int_triple_marker);
    insert_fixed_int_to_byte_span(remainder, value, packed_int_triple_size);
  } else {
    if (std::size(remainder) < 1U + packed_int_octuple_size) {
      return false;
    }
    insert_fixed_int_to_byte_span(remainder, packed_int_octuple_marker);
    insert_fixed_int_to_byte_span(remainder, value, packed_int_octuple_size);
  }

  return true;
}

namespace detail {

template <typename T>
  requires(std::signed_integral<T>)
[[nodiscard]] auto get_signed_representation_as_unsigned(T value) noexcept {
  using unsigned_type = std::make_unsigned_t<T>;
  // sign_mask is 0 if data >= 0 and ~0 if data < 0
  const auto sign_mask{value < static_cast<T>(0)
                           ? std::numeric_limits<unsigned_type>::max()
                           : static_cast<unsigned_type>(0U)};
  auto result{static_cast<unsigned_type>(value)};
  result ^= sign_mask;
  // insert sign bit as the least significant bit
  result <<= 1U;
  result |=
      static_cast<unsigned_type>(sign_mask & static_cast<unsigned_type>(1U));
  return result;
}

} // namespace detail

template <typename T>
  requires(std::unsigned_integral<T>)
[[nodiscard]] std::size_t calculate_varlen_int_size(T value) noexcept {
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/serialization/variable_length_integers.h#L49
  static constexpr std::size_t magic_factor{575U};
  static constexpr std::size_t magic_shift{12U};

  const auto bits_in_value{static_cast<std::size_t>(std::bit_width(value))};
  return ((bits_in_value * magic_factor) >> magic_shift) + 1U;
}

template <typename T>
  requires(std::signed_integral<T>)
[[nodiscard]] std::size_t calculate_varlen_int_size(T value) noexcept {
  return calculate_varlen_int_size(
      detail::get_signed_representation_as_unsigned(value));
}

template <typename T>
  requires(std::unsigned_integral<T>)
[[nodiscard]] bool insert_varlen_int_to_byte_span_checked(byte_span &remainder,
                                                          T value) noexcept {
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/serialization/variable_length_integers.h#L89
  const auto num_bytes{calculate_varlen_int_size(value)};

  if (std::size(remainder) < num_bytes) {
    return false;
  }

  std::uint64_t value_cpy{value};
  // creating a mask with "<num_bytes> - 1" ones in lower digits,
  // for instance '0000 0011' for <num_bytes> == 3 or
  // '0000 0000' for <num_bytes> == 1
  const std::uint8_t first_byte{static_cast<std::uint8_t>(
      ((1ULL << (num_bytes - 1U)) - 1ULL) | (value_cpy << num_bytes))};
  insert_fixed_int_to_byte_span(remainder, first_byte);

  if (num_bytes == 1U) {
    return true;
  }

  // If num_bytes < 8, shift right by 8 - num_bytes.
  // If num_bytes == 8 or num_bytes == 9, no shift is needed.
  if (num_bytes < sizeof(value_cpy)) {
    value_cpy >>= (sizeof(value_cpy) - num_bytes);
  }
  insert_fixed_int_to_byte_span(remainder, value_cpy, num_bytes - 1U);

  return true;
}

template <typename T>
  requires(std::signed_integral<T>)
[[nodiscard]] bool insert_varlen_int_to_byte_span_checked(byte_span &remainder,
                                                          T value) noexcept {
  return insert_varlen_int_to_byte_span_checked(
      remainder, detail::get_signed_representation_as_unsigned(value));
}

} // namespace util

#endif // UTIL_BYTE_SPAN_INSERTERS_HPP
