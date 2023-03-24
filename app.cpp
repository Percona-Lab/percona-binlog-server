#include <array>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <string>
#include <string_view>

#include <boost/json/src.hpp>

#include <mysql/mysql.h>

template <typename T>
[[noreturn]] void raise_exception(
    std::string_view message,
    const std::source_location &location = std::source_location::current()) {
  std::string full_message{location.function_name()};
  full_message += ": ";
  full_message += message;
  throw T{full_message};
}

class connection_config {
public:
  static constexpr std::size_t expected_number_of_parameters = 4;
  using parameter_container =
      std::array<std::string_view, expected_number_of_parameters>;

public:
  [[nodiscard]] connection_config(std::string_view host, std::uint16_t port,
                                  std::string_view user,
                                  std::string_view password)
      : host_{host}, port_{port}, user_{user}, password_{password} {}

  [[nodiscard]] explicit connection_config(std::string_view file_name);

  [[nodiscard]] explicit connection_config(
      const parameter_container &parameters);

  [[nodiscard]] const std::string &get_host() const noexcept { return host_; }
  [[nodiscard]] std::uint16_t get_port() const noexcept { return port_; }
  [[nodiscard]] const std::string &get_user() const noexcept { return user_; }
  [[nodiscard]] const std::string &get_password() const noexcept {
    return password_;
  }

private:
  static constexpr std::string_view key_host{"host"};
  static constexpr std::string_view key_port{"port"};
  static constexpr std::string_view key_user{"user"};
  static constexpr std::string_view key_password{"password"};

  std::string host_;
  std::uint16_t port_;
  std::string user_;
  std::string password_;
};

connection_config::connection_config(const parameter_container &parameters)
    : host_{parameters[0]}, port_{0}, user_{parameters[2]},
      password_{parameters[3]} {
  auto port_bg = parameters[1].data();
  auto port_en = port_bg + parameters[1].size();
  auto [ptr, ec] = std::from_chars(port_bg, port_en, port_);
  if (ec != std::errc() || ptr != port_en)
    raise_exception<std::invalid_argument>("invalid port value");
}

connection_config::connection_config(std::string_view file_name)
    : host_{}, port_{0}, user_{}, password_{} {
  static constexpr std::size_t max_file_size = 1048576;

  std::filesystem::path file_path{file_name};
  std::ifstream ifs{file_path};
  if (!ifs.is_open())
    raise_exception<std::invalid_argument>("cannot open configuration file");
  auto file_size = std::filesystem::file_size(file_path);
  if (file_size == 0)
    raise_exception<std::invalid_argument>("configuration file is empty");
  if (file_size > max_file_size)
    raise_exception<std::invalid_argument>("configuration file is too large");

  std::string file_content(file_size, 'x');
  if (!ifs.read(file_content.data(), static_cast<std::streamoff>(file_size)))
    raise_exception<std::invalid_argument>(
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
    raise_exception<std::invalid_argument>(
        "cannot parse JSON configuration file");
  }
}

int main(int argc, char *argv[]) {
  int exit_code = EXIT_FAILURE;
  if (argc != connection_config::expected_number_of_parameters + 1 &&
      argc != 2) {
    std::filesystem::path executable_path{argv[0]};
    auto filename = executable_path.filename();
    std::cerr << "usage: " << filename << " <host> <port> <user> <password>\n"
              << "       " << filename << " <json_config_file>\n";
    return exit_code;
  }

  try {
    using connection_config_ptr = std::unique_ptr<connection_config>;
    connection_config_ptr config;
    if (argc == 2) {
      std::cout << "Reading connection configuration from the JSON file.\n";
      config = std::make_unique<connection_config>(argv[1]);
    } else if (argc == connection_config::expected_number_of_parameters + 1) {
      std::cout << "Reading connection configuration from the command line "
                   "parameters.\n";
      connection_config::parameter_container parameters;
      std::copy(argv + 1, argv + argc, parameters.begin());
      config = std::make_unique<connection_config>(parameters);
    } else {
      assert(false);
    }
    assert(config);
    std::cout << "host    : " << config->get_host() << '\n';
    std::cout << "port    : " << config->get_port() << '\n';
    std::cout << "user    : " << config->get_user() << '\n';
    std::cout << "password: " << config->get_password() << '\n';

    std::cout << '\n';
    std::cout << "mysql client version: " << mysql_get_client_version() << '\n';

    exit_code = EXIT_SUCCESS;
  } catch (const std::exception &e) {
    std::cerr << "std::exception caught: " << e.what() << '\n';
  } catch (...) {
    std::cerr << "unhandled exception caught\n";
  }

  return exit_code;
}
