#ifndef BINSRV_CONNECTION_CONFIG_HPP
#define BINSRV_CONNECTION_CONFIG_HPP

#include "binsrv/connection_config_fwd.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include "util/command_line_helpers_fwd.hpp"

namespace binsrv {

class connection_config {
public:
  static constexpr std::size_t expected_number_of_arguments = 4;

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  [[nodiscard]] connection_config(std::string_view host, std::uint16_t port,
                                  std::string_view user,
                                  std::string_view password)
      : host_{host}, port_{port}, user_{user}, password_{password} {}
  // NOLINTEND(bugprone-easily-swappable-parameters)

  [[nodiscard]] explicit connection_config(std::string_view file_name);

  [[nodiscard]] explicit connection_config(
      util::command_line_arg_view arguments);

  [[nodiscard]] const std::string &get_host() const noexcept { return host_; }
  [[nodiscard]] std::uint16_t get_port() const noexcept { return port_; }
  [[nodiscard]] const std::string &get_user() const noexcept { return user_; }
  [[nodiscard]] const std::string &get_password() const noexcept {
    return password_;
  }

private:
  std::string host_;
  std::uint16_t port_;
  std::string user_;
  std::string password_;
};

} // namespace binsrv

#endif // BINSRV_CONNECTION_CONFIG_HPP
