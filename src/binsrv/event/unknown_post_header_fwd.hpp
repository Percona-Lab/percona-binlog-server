#ifndef BINSRV_EVENT_UNKNOWN_POST_HEADER_FWD_HPP
#define BINSRV_EVENT_UNKNOWN_POST_HEADER_FWD_HPP

#include <iosfwd>
namespace binsrv::event {

class unknown_post_header;

std::ostream &operator<<(std::ostream &output, const unknown_post_header &obj);

} // namespace binsrv::event

#endif // BINSRV_EVENT_UNKNOWN_POST_HEADER_FWD_HPP
