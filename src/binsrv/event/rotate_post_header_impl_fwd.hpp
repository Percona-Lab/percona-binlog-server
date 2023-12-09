#ifndef BINSRV_EVENT_ROTATE_POST_HEADER_IMPL_FWD_HPP
#define BINSRV_EVENT_ROTATE_POST_HEADER_IMPL_FWD_HPP

#include "binsrv/event/code_type.hpp"
#include "binsrv/event/generic_post_header_fwd.hpp"

namespace binsrv::event {

template <> class generic_post_header_impl<code_type::rotate>;

} // namespace binsrv::event

#endif // BINSRV_EVENT_ROTATE_POST_HEADER_IMPL_FWD_HPP
