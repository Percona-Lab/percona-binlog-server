#ifndef BINSRV_EVENT_FORMAT_DESCRIPTION_POST_HEADER_IMPL_HPP
#define BINSRV_EVENT_FORMAT_DESCRIPTION_POST_HEADER_IMPL_HPP

#include "binsrv/event/format_description_post_header_impl_fwd.hpp" // IWYU pragma: export

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

template <>
class [[nodiscard]] generic_post_header_impl<code_type::format_description> {
public:
  static constexpr std::size_t server_version_length{50U};
  static constexpr std::size_t number_of_events{41U};
  static constexpr std::size_t size_in_bytes{98U};

private:
  using server_version_storage = std::array<std::byte, server_version_length>;
  using post_header_lengths_storage =
      std::array<std::uint8_t, number_of_events>;

public:
  explicit generic_post_header_impl(util::const_byte_span portion);

  [[nodiscard]] std::uint16_t get_binlog_version_raw() const noexcept {
    return binlog_version_;
  }

  [[nodiscard]] const server_version_storage &
  get_server_version_raw() const noexcept {
    return server_version_;
  }

  [[nodiscard]] std::string_view get_server_version() const noexcept;

  [[nodiscard]] std::uint32_t get_create_timestamp_raw() const noexcept {
    return create_timestamp_;
  }
  [[nodiscard]] std::time_t get_create_timestamp() const noexcept {
    return static_cast<std::time_t>(get_create_timestamp_raw());
  }
  [[nodiscard]] std::string get_readable_create_timestamp() const;

  [[nodiscard]] std::uint16_t get_common_header_length_raw() const noexcept {
    return common_header_length_;
  }
  [[nodiscard]] std::size_t get_common_header_length() const noexcept {
    return static_cast<std::size_t>(get_common_header_length_raw());
  }

  [[nodiscard]] const post_header_lengths_storage &
  get_post_header_lengths_raw() const noexcept {
    return post_header_lengths_;
  }
  [[nodiscard]] std::size_t
  get_post_header_length(code_type code) const noexcept;

private:
  // the members are deliberately reordered for better packing
  std::uint32_t create_timestamp_{};                  // 2
  server_version_storage server_version_{};           // 1
  std::uint16_t binlog_version_{};                    // 0
  post_header_lengths_storage post_header_lengths_{}; // 4
  std::uint8_t common_header_length_{};               // 3
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_FORMAT_DESCRIPTION_POST_HEADER_IMPL_HPP
