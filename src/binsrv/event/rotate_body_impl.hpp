#ifndef BINSRV_EVENT_ROTATE_BODY_IMPL_HPP
#define BINSRV_EVENT_ROTATE_BODY_IMPL_HPP

#include "binsrv/event/rotate_body_impl_fwd.hpp" // IWYU pragma: export

#include <string>
#include <string_view>

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

template <> class [[nodiscard]] generic_body_impl<code_type::rotate> {
public:
  // TODO: change this to a container with larger buffer for SBO to avoid
  //       memory allocations (e.g. boost::container::small_vector)
  using binlog_storage = std::string;

  explicit generic_body_impl(util::const_byte_span portion);

  [[nodiscard]] const binlog_storage &get_binlog_raw() noexcept {
    return binlog_;
  }

  [[nodiscard]] std::string_view get_binlog() const noexcept {
    return {binlog_};
  }

private:
  binlog_storage binlog_{};
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_ROTATE_BODY_IMPL_HPP
