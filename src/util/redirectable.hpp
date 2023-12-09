#ifndef UTIL_REDIRECTABLE_FWD_HPP
#define UTIL_REDIRECTABLE_FWD_HPP

namespace util {

template <typename T>
concept redirectable = requires { typename T::redirect_type; };

} // namespace util

#endif // UTIL_REDIRECTABLE_FWD_HPP
