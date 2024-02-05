#include "binsrv/event/unknown_body.hpp"

#include <iosfwd>

namespace binsrv::event {

std::ostream &operator<<(std::ostream &output, const unknown_body & /* obj */) {
  return output;
}

} // namespace binsrv::event
