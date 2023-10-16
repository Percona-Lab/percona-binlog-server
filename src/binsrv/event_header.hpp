#ifndef BINSRV_EVENT_HEADER_HPP
#define BINSRV_EVENT_HEADER_HPP

#include "binsrv/event_header_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

#include "binsrv/event_flag_fwd.hpp"
#include "binsrv/event_type_fwd.hpp"

#include "easymysql/binlog_fwd.hpp"

namespace binsrv {

class [[nodiscard]] event_header {
public:
  explicit event_header(easymysql::binlog_stream_span portion);

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
  [[nodiscard]] event_type get_type_code() const noexcept {
    return static_cast<event_type>(get_type_code_raw());
  }
  [[nodiscard]] std::string_view get_readable_type_code() const noexcept;

  [[nodiscard]] std::uint32_t get_server_id() const noexcept {
    return server_id_;
  }

  [[nodiscard]] std::uint32_t get_event_size() const noexcept {
    return event_size_;
  }

  [[nodiscard]] std::uint32_t get_next_event_position() const noexcept {
    return next_event_position_;
  }

  [[nodiscard]] std::uint16_t get_flags_raw() const noexcept { return flags_; }
  [[nodiscard]] event_flag_set get_flags() const noexcept;
  [[nodiscard]] std::string get_readable_flags() const;

private:
  // the members are deliberately reordered for better packing
  std::uint32_t timestamp_{};
  std::uint32_t server_id_{};
  std::uint32_t event_size_{};
  std::uint32_t next_event_position_{};
  std::uint16_t flags_{};
  std::uint8_t type_code_{};
};

} // namespace binsrv

#endif // BINSRV_EVENT_HEADER_HPP
