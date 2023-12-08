#ifndef BINSRV_EVENT_GENERIC_POST_HEADER_HPP
#define BINSRV_EVENT_GENERIC_POST_HEADER_HPP

#include "binsrv/event/generic_post_header_fwd.hpp" // IWYU pragma: export

#include "binsrv/event/unknown_post_header_fwd.hpp"

#include "util/redirectable.hpp"

namespace binsrv::event {

// This class template is expected to be specialized for individual event type
// codes. The specialization should be constructible from `const_byte_span`.
// template<> class generic_post_header_impl<code_type::xxx> {
// public:
//   generic_post_header_impl<code_type::xxx>(util::const_byte_span portion);
// ...
// };
// Alternatively, the implementation can be "redirected" to another (already
// existing) class via inner 'redirect_type' type alias.
// template<> class generic_post_header_impl<code_type::yyy> {
// public:
//   using redirect_type = empty_post_header;
// ...
// };
// Currently, only two classes 'empty_post_header' and 'unknown_post_header'
// are expected to be used for redirection.
// By default, everything is redirected to 'unknown_post_header' class.
template <code_type Code> class generic_post_header_impl {
public:
  using redirect_type = unknown_post_header;
};

template <code_type Code> struct generic_post_header_helper {
  using type = generic_post_header_impl<Code>;
};

template <code_type Code>
  requires util::redirectable<generic_post_header_impl<Code>>
struct generic_post_header_helper<Code> {
  using type = typename generic_post_header_impl<Code>::redirect_type;
};

template <code_type Code>
using generic_post_header = typename generic_post_header_helper<Code>::type;

} // namespace binsrv::event

#endif // BINSRV_EVENT_GENERIC_POST_HEADER_HPP
