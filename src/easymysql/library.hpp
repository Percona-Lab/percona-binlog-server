#ifndef EASYMYSQL_LIBRARY_HPP
#define EASYMYSQL_LIBRARY_HPP

#include "easymysql/library_fwd.hpp"

#include <cstdint>
#include <string_view>

#include "easymysql/connection_config_fwd.hpp"
#include "easymysql/connection_fwd.hpp"

namespace easymysql {

class [[nodiscard]] library {
public:
  library();
  library(const library &) = delete;
  library(library &&) = delete;
  library &operator=(const library &) = delete;
  library &operator=(library &&) = delete;

  ~library();

  [[nodiscard]] std::uint32_t get_client_version() const noexcept;
  [[nodiscard]] std::string_view get_readable_client_version() const noexcept;

  [[nodiscard]] connection create_connection(const connection_config &config);
};

} // namespace easymysql

#endif // EASYMYSQL_LIBRARY_HPP
