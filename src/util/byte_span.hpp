#ifndef UTIL_BYTE_SPAN_HPP
#define UTIL_BYTE_SPAN_HPP

#include "util/byte_span_fwd.hpp" // IWYU pragma: export

#include <string_view>
namespace util {

inline std::string_view as_string_view(byte_span portion) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return {reinterpret_cast<const char *>(std::data(portion)),
          std::size(portion)};
}

inline std::string_view as_string_view(const_byte_span portion) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return {reinterpret_cast<const char *>(std::data(portion)),
          std::size(portion)};
}

} // namespace util

#endif // UTIL_BYTE_SPAN_HPP
