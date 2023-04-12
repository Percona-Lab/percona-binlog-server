#ifndef UTIL_IMPL_HELPERS_HPP
#define UTIL_IMPL_HELPERS_HPP

#include <memory>

namespace util {

template <typename T> struct deimpl {
  template <typename Deleter>
  [[nodiscard]] static T *get(std::unique_ptr<void, Deleter> &impl) noexcept {
    return static_cast<T *>(impl.get());
  }
  template <typename Deleter>
  [[nodiscard]] static const T *
  get(const std::unique_ptr<void, Deleter> &impl) noexcept {
    return static_cast<const T *>(impl.get());
  }
  // get_const_casted() is supposed to be used to fix broken
  // legacy C interfaces without const quilifiers for read-only
  // parameters
  template <typename Deleter>
  [[nodiscard]] static T *
  get_const_casted(const std::unique_ptr<void, Deleter> &impl) noexcept {
    return static_cast<T *>(impl.get());
  }
};

} // namespace util

#endif // UTIL_IMPL_HELPERS_HPP
