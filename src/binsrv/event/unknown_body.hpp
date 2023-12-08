#ifndef BINSRV_EVENT_UNKNOWN_BODY_HPP
#define BINSRV_EVENT_UNKNOWN_BODY_HPP

#include "binsrv/event/unknown_body_fwd.hpp" // IWYU pragma: export

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

class [[nodiscard]] unknown_body {
public:
  // this class will be used as the first type of the event body variant,
  // so it needs to be default constructible
  unknown_body() noexcept = default;
  explicit unknown_body(util::const_byte_span /*unused*/) noexcept {}
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_UNKNOWN_BODY_HPP
