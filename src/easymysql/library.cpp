#include "easymysql/library.hpp"

#include <mysql/mysql.h>

#include "easymysql/connection.hpp"

#include "util/conversion_helpers.hpp"
#include "util/exception_helpers.hpp"

namespace easymysql {

library::library() {
  if (mysql_library_init(0, nullptr, nullptr) != 0) {
    util::exception_location().raise<std::logic_error>(
        "cannot initialize MySQL library");
  }
}

library::~library() { mysql_library_end(); }

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::uint32_t library::get_client_version() const noexcept {
  return util::maybe_useless_integral_cast<std::uint32_t>(
      mysql_get_client_version());
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::string_view library::get_readable_client_version() const noexcept {
  return {mysql_get_client_info()};
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
connection library::create_connection(const connection_config &config) const {
  return connection(config);
}

} // namespace easymysql
