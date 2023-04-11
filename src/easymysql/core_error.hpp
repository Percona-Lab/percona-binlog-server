#ifndef EASYMYSQL_CORE_ERROR_HPP
#define EASYMYSQL_CORE_ERROR_HPP

#include <stdexcept>
#include <system_error>

namespace easymysql {

[[nodiscard]] const std::error_category &mysql_category() noexcept;

[[nodiscard]] inline std::error_code
make_mysql_error_code(int native_error_code) noexcept {
  return {native_error_code, mysql_category()};
}

class [[nodiscard]] core_error : public std::system_error {
public:
  explicit core_error(int native_error_code);
  core_error(int native_error_code, const std::string &what);
  core_error(int native_error_code, const char *what);
};

} // namespace easymysql

#endif // EASYMYSQL_CORE_ERROR_HPP
