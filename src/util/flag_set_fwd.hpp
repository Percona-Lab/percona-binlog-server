#ifndef UTIL_FLAG_SET_FWD_HPP
#define UTIL_FLAG_SET_FWD_HPP

#include <type_traits>

namespace util {

template <typename E>
requires std::is_enum_v<E>
class flag_set;

} // namespace util

#endif // UTIL_FLAG_SET_FWD_HPP
