#ifndef BINSRV_EVENT_EVENT_FWD_HPP
#define BINSRV_EVENT_EVENT_FWD_HPP

#include <iosfwd>
namespace binsrv::event {

class event;

std::ostream &operator<<(std::ostream &output, const event &obj);

} // namespace binsrv::event

#endif // BINSRV_EVENT_EVENT_FWD_HPP
