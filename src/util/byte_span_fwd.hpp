#ifndef UTIL_BYTE_SPAN_FWD_HPP
#define UTIL_BYTE_SPAN_FWD_HPP

#include <cstddef>
#include <span>

namespace util {

using byte_span = std::span<std::byte>;
using const_byte_span = std::span<const std::byte>;

} // namespace util

#endif // UTIL_BYTE_SPAN_FWD_HPP
