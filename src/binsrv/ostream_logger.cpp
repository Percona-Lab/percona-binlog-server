#include "binsrv/ostream_logger.hpp"

namespace binsrv {

void ostream_logger::do_log(std::string_view message) {
  // flushing here deliberately
  (*stream_) << message << std::endl;
}

} // namespace binsrv
