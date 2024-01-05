#ifndef BINSRV_EVENT_UNKNOWN_POST_HEADER_HPP
#define BINSRV_EVENT_UNKNOWN_POST_HEADER_HPP

#include "binsrv/event/unknown_post_header_fwd.hpp" // IWYU pragma: export

#include <cstddef>

#include "binsrv/event/protocol_traits_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

class [[nodiscard]] unknown_post_header {
public:
  static constexpr std::size_t size_in_bytes{unspecified_post_header_length};

  // this class will be used as the first type of the event post header
  // variant, so it needs to be default constructible
  unknown_post_header() noexcept = default;
  explicit unknown_post_header(util::const_byte_span /*unused*/) noexcept {}
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_UNKNOWN_POST_HEADER_HPP
