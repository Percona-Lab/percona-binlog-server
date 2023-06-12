#include "easymysql/connection_config.hpp"

namespace easymysql {

std::string connection_config::get_connection_string() const {
  return get<"user">() + '@' + get<"host">() + ':' +
         std::to_string(get<"port">()) +
         (has_password() ? " (password ***hidden***)" : " (no password)");
}

} // namespace easymysql
