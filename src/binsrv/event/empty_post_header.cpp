#include "binsrv/event/empty_post_header.hpp"

#include <iterator>
#include <stdexcept>

#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

empty_post_header::empty_post_header(util::const_byte_span portion) {
  if (std::size(portion) != size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid event empty post header length");
  }
}

} // namespace binsrv::event
