#ifndef UTIL_EXCEPTION_HELPERS_HPP
#define UTIL_EXCEPTION_HELPERS_HPP

#include <source_location>
#include <string>
#include <string_view>

namespace util {

template <typename T>
[[noreturn]] void raise_exception(
    std::string_view message,
    const std::source_location &location = std::source_location::current()) {
  std::string full_message{location.function_name()};
  full_message += ": ";
  full_message += message;
  throw T{full_message};
}

} // namespace util

#endif // UTIL_EXCEPTION_HELPERS_HPP
