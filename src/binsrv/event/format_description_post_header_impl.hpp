// Copyright (c) 2023-2024 Percona and/or its affiliates.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 2.0,
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License, version 2.0, for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

#ifndef BINSRV_EVENT_FORMAT_DESCRIPTION_POST_HEADER_IMPL_HPP
#define BINSRV_EVENT_FORMAT_DESCRIPTION_POST_HEADER_IMPL_HPP

#include "binsrv/event/format_description_post_header_impl_fwd.hpp" // IWYU pragma: export

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

#include "binsrv/event/protocol_traits.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

template <>
class [[nodiscard]] generic_post_header_impl<code_type::format_description> {
public:
  static constexpr std::size_t server_version_length{50U};
  static constexpr std::size_t size_in_bytes{98U};

  using server_version_storage = std::array<std::byte, server_version_length>;

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

  [[nodiscard]] const post_header_length_container &
  get_post_header_lengths_raw() const noexcept {
    return post_header_lengths_;
  }
  [[nodiscard]] std::size_t
  get_post_header_length(code_type code) const noexcept {
    return get_post_header_length_for_code(post_header_lengths_, code);
  }

private:
  // the members are deliberately reordered for better packing
  std::uint32_t create_timestamp_{};                  // 2
  server_version_storage server_version_{};           // 1
  std::uint16_t binlog_version_{};                    // 0
  post_header_length_container post_header_lengths_{}; // 4
  std::uint8_t common_header_length_{};               // 3
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_FORMAT_DESCRIPTION_POST_HEADER_IMPL_HPP
