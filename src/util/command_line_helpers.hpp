// Copyright (c) 2023-2024 Percona and/or its affiliates.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 2.0,
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License, version 2.0, for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

#ifndef UTIL_COMMAND_LINE_HELPERS_HPP
#define UTIL_COMMAND_LINE_HELPERS_HPP

#include "util/command_line_helpers_fwd.hpp" // IWYU pragma: export

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
extract_executable_name(command_line_arg_view cmd_args);

[[nodiscard]] std::string
get_readable_command_line_arguments(command_line_arg_view cmd_args);

} // namespace util

#endif // UTIL_COMMAND_LINE_HELPERS_HPP
