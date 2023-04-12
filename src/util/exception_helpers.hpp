#ifndef UTIL_EXCEPTION_HELPERS_HPP
#define UTIL_EXCEPTION_HELPERS_HPP

#include <concepts>
#include <exception>
#include <source_location>
#include <string>
#include <string_view>

namespace util {

template <std::derived_from<std::exception> Exception>
class [[nodiscard]] location_exception_adapter : public Exception,
                                                 public std::source_location {
public:
  using base_type = Exception;
  using mixin_type = std::source_location;

  template <typename... TT>
  explicit location_exception_adapter(std::source_location location,
                                      TT &&...args)
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
      : base_type{std::forward<TT>(args)...}, mixin_type{location} {}

  [[nodiscard]] std::source_location get_location() const noexcept {
    return *static_cast<const mixin_type *>(this);
  }
};

// deliberately without [[nodiscard]] as this class is supposed to be used
// as a helper only
// e.g. exception_location().raise<std::invalid_argument>(
//   "value cannot be zero")
class exception_location {
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

#endif // UTIL_EXCEPTION_HELPERS_HPP
