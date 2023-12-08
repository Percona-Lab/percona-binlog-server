#ifndef BINSRV_EVENT_FORMAT_DESCRIPTION_BODY_IMPL_FWD_HPP
#define BINSRV_EVENT_FORMAT_DESCRIPTION_BODY_IMPL_FWD_HPP

#include "binsrv/event/code_type.hpp"
#include "binsrv/event/generic_body_fwd.hpp"

namespace binsrv::event {

template <> class generic_body_impl<code_type::format_description>;

} // namespace binsrv::event

#endif // BINSRV_EVENT_FORMAT_DESCRIPTION_BODY_IMPL_FWD_HPP
