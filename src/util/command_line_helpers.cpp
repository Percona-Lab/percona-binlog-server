#include "util/command_line_helpers.hpp"

#include <filesystem>
#include <string>

namespace util {

std::string extract_executable_name(util::command_line_arg_view cmd_args) {
  std::string res;
  if (!cmd_args.empty()) {
    const std::filesystem::path executable_path{cmd_args[0]};
    auto filename = executable_path.filename();
    res = filename.string();
  }
  if (res.empty()) {
    res = "executable";
  }
  return res;
}

std::string
get_readable_command_line_arguments(util::command_line_arg_view cmd_args) {
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
