#ifndef BINSRV_EVENT_COMMON_HEADER_HPP
#define BINSRV_EVENT_COMMON_HEADER_HPP

#include "binsrv/event/common_header_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

#include "binsrv/event/code_type_fwd.hpp"
#include "binsrv/event/flag_type_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

class [[nodiscard]] common_header {
public:
  static constexpr std::size_t size_in_bytes{19U};

  explicit common_header(util::const_byte_span portion);

  [[nodiscard]] std::uint32_t get_timestamp_raw() const noexcept {
    return timestamp_;
  }
  [[nodiscard]] std::time_t get_timestamp() const noexcept {
    return static_cast<std::time_t>(get_timestamp_raw());
  }
  [[nodiscard]] std::string get_readable_timestamp() const;

  [[nodiscard]] std::uint8_t get_type_code_raw() const noexcept {
    return type_code_;
  }
  [[nodiscard]] code_type get_type_code() const noexcept {
    return static_cast<code_type>(get_type_code_raw());
  }
  [[nodiscard]] std::string_view get_readable_type_code() const noexcept;

  [[nodiscard]] std::uint32_t get_server_id_raw() const noexcept {
    return server_id_;
  }

  [[nodiscard]] std::uint32_t get_event_size_raw() const noexcept {
    return event_size_;
  }

  [[nodiscard]] std::uint32_t get_next_event_position_raw() const noexcept {
    return next_event_position_;
  }

  [[nodiscard]] std::uint16_t get_flags_raw() const noexcept { return flags_; }
  [[nodiscard]] flag_set get_flags() const noexcept;
  [[nodiscard]] std::string get_readable_flags() const;

private:
  // the members are deliberately reordered for better packing
  std::uint32_t timestamp_{};           // 0
  std::uint32_t server_id_{};           // 2
  std::uint32_t event_size_{};          // 3
  std::uint32_t next_event_position_{}; // 4
  std::uint16_t flags_{};               // 5
  std::uint8_t type_code_{};            // 1
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_COMMON_HEADER_HPP
