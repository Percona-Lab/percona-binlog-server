#ifndef UTIL_COMMAND_LINE_HELPERS_HPP
#define UTIL_COMMAND_LINE_HELPERS_HPP

#include "util/command_line_helpers_fwd.hpp"

#include <iterator>

namespace util {

inline command_line_arg_view to_command_line_agg_view(
    int argc,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
    char *argv[]) noexcept {
  const char *const *const const_argv = argv;
  return {const_argv, static_cast<std::size_t>(argc)};
}

[[nodiscard]] std::string
extract_executable_name(util::command_line_arg_view cmd_args);

} // namespace util

#endif // UTIL_COMMAND_LINE_HELPERS_HPP
