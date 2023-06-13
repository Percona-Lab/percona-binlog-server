#ifndef EASYMYSQL_CONNECTION_CONFIG_HPP
#define EASYMYSQL_CONNECTION_CONFIG_HPP

#include "easymysql/connection_config_fwd.hpp"

#include <cstdint>
#include <string>

#include "util/nv_tuple.hpp"

namespace easymysql {

// NOLINTNEXTLINE(bugprone-exception-escape)
struct [[nodiscard]] connection_config
    : util::nv_tuple<
          // clang-format off
          util::nv<"host"    , std::string>,
          util::nv<"port"    , std::uint16_t>,
          util::nv<"user"    , std::string>,
          util::nv<"password", std::string>
          // clang-format on
          > {
  [[nodiscard]] bool has_password() const noexcept {
    return !get<"password">().empty();
  }

  [[nodiscard]] std::string get_connection_string() const;
};

} // namespace easymysql

#endif // EASYMYSQL_CONNECTION_CONFIG_HPP
