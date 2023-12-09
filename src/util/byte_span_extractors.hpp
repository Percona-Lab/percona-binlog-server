#ifndef UTIL_BYTE_SPAN_EXTRACTORS_HPP
#define UTIL_BYTE_SPAN_EXTRACTORS_HPP

#include <array>
#include <cassert>
// probably a bug in IWYU: <concepts> is required by std::integral
// TODO: check if the same bug exust in clang-17
#include <concepts> // IWYU pragma: keep
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
