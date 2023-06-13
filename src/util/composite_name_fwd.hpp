#ifndef UTIL_COMPOSIT_NAME_FWD_HPP
#define UTIL_COMPOSIT_NAME_FWD_HPP

#include <string>
#include <string_view>

namespace util {

template <typename StringLikeType> class composite_name;

using string_composite_name = composite_name<std::string>;
using string_view_composite_name = composite_name<std::string_view>;
using c_str_composite_name = composite_name<const char *>;

} // namespace util

#endif // UTIL_COMPOSIT_NAME_FWD_HPP
