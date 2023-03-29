#include "util/command_line_helpers.hpp"

#include <filesystem>
#include <string>

namespace util {

std::string extract_executable_name(util::command_line_arg_view cmd_args) {
  std::string res;
  if (!cmd_args.empty()) {
    try {
      const std::filesystem::path executable_path{cmd_args[0]};
      auto filename = executable_path.filename();
    } catch (const std::exception &) {
    }
  }
  if (res.empty()) {
    res = "executable";
  }
  return res;
}

} // namespace util
