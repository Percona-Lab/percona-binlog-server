#include "binsrv/master_config.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/json/parse.hpp>
#include <boost/json/src.hpp> // IWYU pragma: keep

// Needed for log_severity's operator <<
#include "binsrv/log_severity.hpp" // IWYU pragma: keep

#include "util/command_line_helpers_fwd.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/nv_tuple_from_command_line.hpp"
#include "util/nv_tuple_from_json.hpp"

namespace binsrv {

master_config::master_config(util::command_line_arg_view arguments) {
  util::nv_tuple_from_command_line(arguments, impl_);
}

master_config::master_config(std::string_view file_name) {
  static constexpr std::size_t max_file_size{1048576U};

  const std::filesystem::path file_path{file_name};
  std::ifstream ifs{file_path};
  if (!ifs.is_open()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot open configuration file");
  }
  auto file_size = std::filesystem::file_size(file_path);
  if (file_size == 0ULL) {
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

  auto json_value = boost::json::parse(file_content);
  util::nv_tuple_from_json(json_value, impl_);
}

} // namespace binsrv
