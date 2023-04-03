#ifndef EASYMYSQL_CONNECTION_CONFIG_HPP
#define EASYMYSQL_CONNECTION_CONFIG_HPP

#include "easymysql/connection_config_fwd.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include "util/command_line_helpers_fwd.hpp"

namespace easymysql {

class [[nodiscard]] connection_config {
public:
  static constexpr std::size_t expected_number_of_arguments = 4;

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  connection_config(std::string_view host, std::uint16_t port,
                    std::string_view user, std::string_view password)
      : host_{host}, port_{port}, user_{user}, password_{password} {}
  // NOLINTEND(bugprone-easily-swappable-parameters)

  explicit connection_config(std::string_view file_name);

  explicit connection_config(util::command_line_arg_view arguments);

  [[nodiscard]] const std::string &get_host() const noexcept { return host_; }
  [[nodiscard]] std::uint16_t get_port() const noexcept { return port_; }
  [[nodiscard]] const std::string &get_user() const noexcept { return user_; }
  [[nodiscard]] const std::string &get_password() const noexcept {
    return password_;
  }
  [[nodiscard]] bool has_password() const noexcept {
    return !password_.empty();
  }

  [[nodiscard]] std::string get_connection_string() const;

private:
  std::string host_;
  std::uint16_t port_;
  std::string user_;
  std::string password_;
};

} // namespace easymysql

#endif // EASYMYSQL_CONNECTION_CONFIG_HPP
