#include "easymysql/connection_config.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include <boost/json/src.hpp>

#include "util/exception_helpers.hpp"

namespace {

constexpr std::string_view key_host{"host"};
constexpr std::string_view key_port{"port"};
constexpr std::string_view key_user{"user"};
constexpr std::string_view key_password{"password"};

} // anonymous namespace

namespace easymysql {

connection_config::connection_config(util::command_line_arg_view arguments)
    : host_{arguments[1]}, port_{0}, user_{arguments[3]},
      password_{arguments[4]} {
  const auto *port_bg = arguments[2];
  const auto *port_en = std::next(
      port_bg, static_cast<std::ptrdiff_t>(std::strlen(arguments[2])));
  auto [ptr, ec] = std::from_chars(port_bg, port_en, port_);
  if (ec != std::errc() || ptr != port_en) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid port value");
  }
}

connection_config::connection_config(std::string_view file_name)
    : host_{}, port_{0}, user_{}, password_{} {
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
  if (!ifs.read(file_content.data(), static_cast<std::streamoff>(file_size))) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot read configuration file content");
  }

  try {
    auto json_value = boost::json::parse(file_content);
    const auto &json_object = json_value.as_object();

    host_ = boost::json::value_to<decltype(host_)>(json_object.at(key_host));
    port_ = boost::json::value_to<decltype(port_)>(json_object.at(key_port));
    user_ = boost::json::value_to<decltype(user_)>(json_object.at(key_user));
    password_ = boost::json::value_to<decltype(password_)>(
        json_object.at(key_password));

  } catch (const std::exception &) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot parse JSON configuration file");
  }
}

std::string connection_config::get_connection_string() const {
  return get_user() + '@' + get_host() + ':' + std::to_string(get_port()) +
         (has_password() ? " (password ***hidden***)" : " (no password)");
}

} // namespace easymysql
