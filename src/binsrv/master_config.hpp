#ifndef BINSRV_MASTER_CONFIG_HPP
#define BINSRV_MASTER_CONFIG_HPP

#include "binsrv/master_config_fwd.hpp"

#include "binsrv/logger_config.hpp"

#include "easymysql/connection_config.hpp"

#include "util/command_line_helpers_fwd.hpp"
#include "util/nv_tuple.hpp"

namespace binsrv {

class [[nodiscard]] master_config {
private:
  using impl_type = util::nv_tuple<
      // clang-format off
      util::nv<"logger"    , logger_config>,
      util::nv<"connection", easymysql::connection_config>
      // clang-format on
      >;

public:
  static constexpr std::size_t flattened_size = impl_type::flattened_size;
  static constexpr std::size_t depth = impl_type::depth;

  explicit master_config(std::string_view file_name);

  explicit master_config(util::command_line_arg_view arguments);

  [[nodiscard]] const auto &root() const noexcept { return impl_; }

private:
  impl_type impl_;
};

} // namespace binsrv

#endif // BINSRV_MASTER_CONFIG_HPP
