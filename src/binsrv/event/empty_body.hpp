#ifndef BINSRV_EVENT_EMPTY_BODY_HPP
#define BINSRV_EVENT_EMPTY_BODY_HPP

#include "binsrv/event/empty_post_header_fwd.hpp" // IWYU pragma: export

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

class [[nodiscard]] empty_body {
public:
  static constexpr std::size_t size_in_bytes{0U};

  explicit empty_body(util::const_byte_span portion);
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_EMPTY_BODY_HPP
