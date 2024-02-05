#ifndef BINSRV_EVENT_COMMON_HEADER_FWD_HPP
#define BINSRV_EVENT_COMMON_HEADER_FWD_HPP

#include <iosfwd>

namespace binsrv::event {

class common_header;

std::ostream &operator<<(std::ostream &output, const common_header &obj);

} // namespace binsrv::event

#endif // BINSRV_EVENT_COMMON_HEADER_FWD_HPP
