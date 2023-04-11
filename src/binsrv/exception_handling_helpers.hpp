#ifndef BINSRV_EXCEPTION_HANDLING_HELPERS_HPP
#define BINSRV_EXCEPTION_HANDLING_HELPERS_HPP

#include "binsrv/basic_logger_fwd.hpp"

namespace binsrv {

void handle_std_exception(const basic_logger_ptr &logger);

} // namespace binsrv

#endif // BINSRV_EXCEPTION_HANDLING_HELPERS_HPP
