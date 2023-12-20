#ifndef BINSRV_EVENT_READER_CONTEXT_HPP
#define BINSRV_EVENT_READER_CONTEXT_HPP

#include "binsrv/event/reader_context_fwd.hpp" // IWYU pragma: export

#include "binsrv/event/checksum_algorithm_type_fwd.hpp"
#include "binsrv/event/event_fwd.hpp"
#include "binsrv/event/protocol_traits.hpp"

namespace binsrv::event {

class [[nodiscard]] reader_context {
  friend class event;

public:
  reader_context();

  [[nodiscard]] bool has_fde_processed() const noexcept {
    return fde_processed_;
  }
  [[nodiscard]] checksum_algorithm_type
  get_current_checksum_algorithm() const noexcept;
  [[nodiscard]] std::size_t
  get_current_post_header_length(code_type code) const noexcept;

private:
  bool fde_processed_{false};
  // NOLINTNEXTLINE(cppcoreguidelines-use-default-member-init,modernize-use-default-member-init)
  checksum_algorithm_type checksum_algorithm_;
  post_header_length_container post_header_lengths_{};

  void process_event(const event &current_event);
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_READER_CONTEXT_HPP
