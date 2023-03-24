#include "binsrv/connection_config.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <boost/json/src.hpp>

#include "util/exception_helpers.hpp"

namespace {

constexpr std::string_view key_host{"host"};
constexpr std::string_view key_port{"port"};
constexpr std::string_view key_user{"user"};
constexpr std::string_view key_password{"password"};

} // anonymous namespace

namespace binsrv {

connection_config::connection_config(const parameter_container &parameters)
    : host_{parameters[0]}, port_{0}, user_{parameters[2]},
      password_{parameters[3]} {
  auto port_bg = parameters[1].data();
  auto port_en = port_bg + parameters[1].size();
  auto [ptr, ec] = std::from_chars(port_bg, port_en, port_);
  if (ec != std::errc() || ptr != port_en)
    util::raise_exception<std::invalid_argument>("invalid port value");
}

connection_config::connection_config(std::string_view file_name)
    : host_{}, port_{0}, user_{}, password_{} {
  static constexpr std::size_t max_file_size = 1048576;

  std::filesystem::path file_path{file_name};
  std::ifstream ifs{file_path};
  if (!ifs.is_open())
    util::raise_exception<std::invalid_argument>(
        "cannot open configuration file");
  auto file_size = std::filesystem::file_size(file_path);
  if (file_size == 0)
    util::raise_exception<std::invalid_argument>("configuration file is empty");
  if (file_size > max_file_size)
    util::raise_exception<std::invalid_argument>(
        "configuration file is too large");

  std::string file_content(file_size, 'x');
  if (!ifs.read(file_content.data(), static_cast<std::streamoff>(file_size)))
    util::raise_exception<std::invalid_argument>(
        "cannot read configuration file content");

  try {
    auto json_value = boost::json::parse(file_content);
    const auto &json_object = json_value.as_object();

    host_ = boost::json::value_to<decltype(host_)>(json_object.at(key_host));
    port_ = boost::json::value_to<decltype(port_)>(json_object.at(key_port));
    user_ = boost::json::value_to<decltype(user_)>(json_object.at(key_user));
    password_ = boost::json::value_to<decltype(password_)>(
        json_object.at(key_password));

  } catch (const std::exception &) {
    util::raise_exception<std::invalid_argument>(
        "cannot parse JSON configuration file");
  }
}

} // namespace binsrv
