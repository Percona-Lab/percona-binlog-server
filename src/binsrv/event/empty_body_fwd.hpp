#ifndef BINSRV_EVENT_EMPTY_BODY_FWD_HPP
#define BINSRV_EVENT_EMPTY_BODY_FWD_HPP

#include <iosfwd>

namespace binsrv::event {

class empty_body;

std::ostream &operator<<(std::ostream &output, const enpty_body &obj);

} // namespace binsrv::event

#endif // BINSRV_EVENT_EMPTY_BODY_FWD_HPP
