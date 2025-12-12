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

#include "util/command_line_helpers.hpp"

#include <filesystem>
#include <string>

namespace util {

std::string extract_executable_name(command_line_arg_view cmd_args) {
  std::string res;
  if (!cmd_args.empty()) {
    const std::filesystem::path executable_path{cmd_args[0U]};
    auto filename = executable_path.filename();
    res = filename.string();
  }
  if (res.empty()) {
    res = "executable";
  }
  return res;
}

std::string
get_readable_command_line_arguments(command_line_arg_view cmd_args) {
  std::string res;
  if (cmd_args.empty()) {
    return res;
  }
  const auto shifted_cmd_args = cmd_args.subspan(1U);

  bool first = true;
  for (const auto *arg : shifted_cmd_args) {
    if (first) {
      first = false;
    } else {
      res += ' ';
    }
    res += '"';
    res += arg;
    res += '"';
  }
  return res;
}

} // namespace util
