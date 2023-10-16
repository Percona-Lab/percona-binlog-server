#ifndef BINSRV_LOGGER_CONFIG_HPP
#define BINSRV_LOGGER_CONFIG_HPP

#include "binsrv/logger_config_fwd.hpp" // IWYU pragma: export

#include <string>

#include "binsrv/log_severity_fwd.hpp"

#include "util/nv_tuple.hpp"

namespace binsrv {

// NOLINTNEXTLINE(bugprone-exception-escape)
struct [[nodiscard]] logger_config
    : util::nv_tuple<
          // clang-format off
          util::nv<"level", log_severity>,
          util::nv<"file" , std::string>
          // clang-format on
          > {
  [[nodiscard]] bool has_file() const noexcept {
    return !get<"file">().empty();
  }
};

} // namespace binsrv

#endif // BINSRV_LOGGER_CONFIG_HPP
