#include "binsrv/logger_factory.hpp"

#include <memory>

#include "binsrv/cout_logger.hpp"
#include "binsrv/file_logger.hpp"
#include "binsrv/logger_config.hpp"

namespace binsrv {
basic_logger_ptr logger_factory::create(const logger_config &config) {
  const auto level = config.get<"level">();
  if (!config.has_file()) {
    return std::make_shared<cout_logger>(level);
  }

  return std::make_shared<file_logger>(level, config.get<"file">());
}

} // namespace binsrv
