#ifndef BINSRV_LOGGER_FACTORY_HPP
#define BINSRV_LOGGER_FACTORY_HPP

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/logger_config_fwd.hpp"

namespace binsrv {

struct [[nodiscard]] logger_factory {
  [[nodiscard]] static basic_logger_ptr create(const logger_config &config);
};

} // namespace binsrv

#endif // BINSRV_LOGGER_FACTORY_HPP
