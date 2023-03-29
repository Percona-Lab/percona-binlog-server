#ifndef BINSRV_BASIC_LOGGER_FWD_HPP
#define BINSRV_BASIC_LOGGER_FWD_HPP

#include <memory>

namespace binsrv {

enum class log_severity;

class basic_logger;

using basic_logger_ptr = std::shared_ptr<basic_logger>;

} // namespace binsrv

#endif // BINSRV_BASIC_LOGGER_FWD_HPP
