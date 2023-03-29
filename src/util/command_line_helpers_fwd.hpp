#ifndef UTIL_COMMAND_LINE_HELPERS_FWD_HPP
#define UTIL_COMMAND_LINE_HELPERS_FWD_HPP

#include <span>

namespace util {

using command_line_arg_view = std::span<const char *const>;

} // namespace util

#endif // UTIL_COMMAND_LINE_HELPERS_FWD_HPP
