#ifndef BINSRV_EVENT_FOOTER_FWD_HPP
#define BINSRV_EVENT_FOOTER_FWD_HPP

#include <iosfwd>
namespace binsrv::event {

class footer;

std::ostream &operator<<(std::ostream &output, const footer &obj);

} // namespace binsrv::event

#endif // BINSRV_EVENT_FOOTER_FWD_HPP
