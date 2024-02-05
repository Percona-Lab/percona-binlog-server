#include "binsrv/event/unknown_post_header.hpp"

#include <iosfwd>

namespace binsrv::event {

std::ostream &operator<<(std::ostream &output,
                         const unknown_post_header & /* obj */) {
  return output;
}

} // namespace binsrv::event
