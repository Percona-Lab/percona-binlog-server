#ifndef BINSRV_EVENT_FORMAT_DESCRIPTION_BODY_IMPL_FWD_HPP
#define BINSRV_EVENT_FORMAT_DESCRIPTION_BODY_IMPL_FWD_HPP

#include <iosfwd>

#include "binsrv/event/code_type.hpp"
#include "binsrv/event/generic_body_fwd.hpp"

namespace binsrv::event {

template <> class generic_body_impl<code_type::format_description>;

std::ostream &
operator<<(std::ostream &output,
           const generic_body_impl<code_type::format_description> &obj);

} // namespace binsrv::event

#endif // BINSRV_EVENT_FORMAT_DESCRIPTION_BODY_IMPL_FWD_HPP
