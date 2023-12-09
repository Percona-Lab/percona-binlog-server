#include "binsrv/event/empty_body.hpp"

#include <iterator>
#include <stdexcept>

#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

empty_body::empty_body(util::const_byte_span portion) {
  if (std::size(portion) != size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid event empty body length");
  }
}

} // namespace binsrv::event
