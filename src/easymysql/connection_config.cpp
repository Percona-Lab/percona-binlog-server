#include "easymysql/connection_config.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include <boost/json/src.hpp>

#include "util/exception_helpers.hpp"
#include "util/nv_tuple_from_command_line.hpp"
#include "util/nv_tuple_from_json.hpp"

namespace easymysql {

connection_config::connection_config(util::command_line_arg_view arguments) {
  try {
    util::nv_tuple_from_command_line(arguments, *this);
  } catch (const std::exception &) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot extract configuration values from the command line");
  }
}

connection_config::connection_config(std::string_view file_name) {
  static constexpr std::size_t max_file_size = 1048576;

  const std::filesystem::path file_path{file_name};
  std::ifstream ifs{file_path};
  if (!ifs.is_open()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot open configuration file");
  }
  auto file_size = std::filesystem::file_size(file_path);
  if (file_size == 0) {
    util::exception_location().raise<std::invalid_argument>(
        "configuration file is empty");
  }
  if (file_size > max_file_size) {
    util::exception_location().raise<std::invalid_argument>(
        "configuration file is too large");
  }

  std::string file_content(file_size, 'x');
  if (!ifs.read(std::data(file_content),
                static_cast<std::streamoff>(file_size))) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot read configuration file content");
  }

  try {
    auto json_value = boost::json::parse(file_content);
    util::nv_tuple_from_json(json_value, *this);
  } catch (const std::exception &) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot parse JSON configuration file");
  }
}

std::string connection_config::get_connection_string() const {
  return get<"user">() + '@' + get<"host">() + ':' +
         std::to_string(get<"port">()) +
         (has_password() ? " (password ***hidden***)" : " (no password)");
}

} // namespace easymysql
