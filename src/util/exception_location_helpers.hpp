#ifndef UTIL_EXCEPTION_LOCATION_HELPERS_HPP
#define UTIL_EXCEPTION_LOCATION_HELPERS_HPP

#include <concepts>
#include <exception>
#include <source_location>

#include "util/mixin_exception_adapter.hpp"

namespace util {

template <std::derived_from<std::exception> Exception>
using location_exception_adapter =
    mixin_exception_adapter<Exception, std::source_location>;

// this class is supposed to be used as a helper only
// e.g. exception_location().raise<std::invalid_argument>(
//   "value cannot be zero")
class [[nodiscard]] exception_location {
public:
  explicit exception_location(
      std::source_location location = std::source_location::current())
      : location_{location} {}
  template <std::derived_from<std::exception> Exception, typename... TT>
  [[noreturn]] void raise(TT &&...args) const {
    using wrapped_exception = location_exception_adapter<Exception>;
    throw wrapped_exception{location_, std::forward<TT>(args)...};
  }

private:
  std::source_location location_;
};

} // namespace util

#endif // UTIL_EXCEPTION_LOCATION_HELPERS_HPP
