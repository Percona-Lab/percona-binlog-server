#ifndef EASYMYSQL_CONNECTION_CONFIG_HPP
#define EASYMYSQL_CONNECTION_CONFIG_HPP

#include "easymysql/connection_config_fwd.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include "util/command_line_helpers_fwd.hpp"
#include "util/nv_tuple.hpp"

namespace easymysql {

class [[nodiscard]] connection_config
    : public util::nv_tuple<
          // clang-format off
          util::nv<"host"    , std::string>,
          util::nv<"port"    , std::uint16_t>,
          util::nv<"user"    , std::string>,
          util::nv<"password", std::string>
          // clang-format on
          > {
public:
  explicit connection_config(std::string_view file_name);

  explicit connection_config(util::command_line_arg_view arguments);

  [[nodiscard]] bool has_password() const noexcept {
    return !get<"password">().empty();
  }

  [[nodiscard]] std::string get_connection_string() const;
};

} // namespace easymysql

#endif // EASYMYSQL_CONNECTION_CONFIG_HPP
