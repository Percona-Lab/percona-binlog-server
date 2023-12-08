#ifndef BINSRV_EVENT_ROTATE_POST_HEADER_IMPL_HPP
#define BINSRV_EVENT_ROTATE_POST_HEADER_IMPL_HPP

#include "binsrv/event/rotate_post_header_impl_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <cstdint>

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

template <> class [[nodiscard]] generic_post_header_impl<code_type::rotate> {
public:
  static constexpr std::size_t size_in_bytes{8U};

  explicit generic_post_header_impl(util::const_byte_span portion);

  [[nodiscard]] std::uint64_t get_position_id_raw() const noexcept {
    return position_;
  }

private:
  std::uint64_t position_{};
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_ROTATE_POST_HEADER_IMPL_HPP
