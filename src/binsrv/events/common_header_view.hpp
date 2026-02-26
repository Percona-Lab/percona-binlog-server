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

#ifndef BINSRV_EVENTS_COMMON_HEADER_VIEW_HPP
#define BINSRV_EVENTS_COMMON_HEADER_VIEW_HPP

#include "binsrv/events/common_header_view_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <string>
#include <string_view>

#include "binsrv/ctime_timestamp_fwd.hpp"

#include "binsrv/events/code_type_fwd.hpp"
#include "binsrv/events/common_header_flag_type_fwd.hpp"
#include "binsrv/events/protocol_traits_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::events {

class [[nodiscard]] common_header_view_base {
public:
  static constexpr std::size_t timestamp_offset{0U};
  static constexpr std::size_t type_code_offset{timestamp_offset +
                                                sizeof(std::uint32_t)};
  static constexpr std::size_t server_id_offset{type_code_offset +
                                                sizeof(std::uint8_t)};
  static constexpr std::size_t event_size_offset{server_id_offset +
                                                 sizeof(std::uint32_t)};
  static constexpr std::size_t next_event_position_offset{
      event_size_offset + sizeof(std::uint32_t)};
  static constexpr std::size_t flags_offset{next_event_position_offset +
                                            sizeof(std::uint32_t)};

  static constexpr std::size_t size_in_bytes{flags_offset +
                                             sizeof(std::uint16_t)};
  static_assert(size_in_bytes == default_common_header_length);

  // timestamp section
  [[nodiscard]] static ctime_timestamp
  get_timestamp_from_raw(std::uint32_t timestamp) noexcept;
  [[nodiscard]] static std::string
  get_readable_timestamp_from_raw(std::uint32_t timestamp);

  // type_code section
  [[nodiscard]] static code_type
  get_type_code_from_raw(std::uint8_t type_code) noexcept;
  [[nodiscard]] static std::string_view
  get_readable_type_code_from_raw(std::uint8_t type_code) noexcept;

  // flags section
  [[nodiscard]] static common_header_flag_set
  get_flags_from_raw(std::uint16_t flags) noexcept;
  [[nodiscard]] static std::string
  get_readable_flags_from_raw(std::uint16_t flags);

protected:
  explicit common_header_view_base(util::byte_span portion);

  // timestamp section
  [[nodiscard]] std::uint32_t get_timestamp_raw() const noexcept;
  [[nodiscard]] ctime_timestamp get_timestamp() const noexcept;
  [[nodiscard]] std::string get_readable_timestamp() const {
    return get_readable_timestamp_from_raw(get_timestamp_raw());
  }
  void set_timestamp_raw(std::uint32_t timestamp) const noexcept;

  // type_code section
  [[nodiscard]] std::uint8_t get_type_code_raw() const noexcept;
  [[nodiscard]] code_type get_type_code() const noexcept {
    return get_type_code_from_raw(get_type_code_raw());
  }
  [[nodiscard]] std::string_view get_readable_type_code() const noexcept {
    return get_readable_type_code_from_raw(get_type_code_raw());
  }
  void set_type_code_raw(std::uint8_t type_code) const noexcept;

  // server_id section
  [[nodiscard]] std::uint32_t get_server_id_raw() const noexcept;
  void set_server_id_raw(std::uint32_t server_id) const noexcept;

  // event_size section
  [[nodiscard]] std::uint32_t get_event_size_raw() const noexcept;
  void set_event_size_raw(std::uint32_t event_size) const noexcept;

  // next_event_position section
  [[nodiscard]] std::uint32_t get_next_event_position_raw() const noexcept;
  void
  set_next_event_position_raw(std::uint32_t next_event_position) const noexcept;

  // flags section
  [[nodiscard]] std::uint16_t get_flags_raw() const noexcept;
  [[nodiscard]] common_header_flag_set get_flags() const noexcept;
  [[nodiscard]] std::string get_readable_flags() const {
    return get_readable_flags_from_raw(get_flags_raw());
  }
  void set_flags_raw(std::uint16_t flags) const noexcept;

private:
  util::byte_span portion_{};
};

class [[nodiscard]] common_header_updatable_view
    : private common_header_view_base {
  friend class common_header_view;

public:
  explicit common_header_updatable_view(util::byte_span portion)
      : common_header_view_base{portion} {}

  // clang-format off
  // timestamp section
  using common_header_view_base::get_timestamp_raw;
  using common_header_view_base::get_timestamp;
  using common_header_view_base::get_readable_timestamp;
  using common_header_view_base::set_timestamp_raw;

  // type_code section
  using common_header_view_base::get_type_code_raw;
  using common_header_view_base::get_type_code;
  using common_header_view_base::get_readable_type_code;
  using common_header_view_base::set_type_code_raw;

  // server_id section
  using common_header_view_base::get_server_id_raw;
  using common_header_view_base::set_server_id_raw;

  // event_size section
  using common_header_view_base::get_event_size_raw;
  using common_header_view_base::set_event_size_raw;

  // next_event_position section
  using common_header_view_base::get_next_event_position_raw;
  using common_header_view_base::set_next_event_position_raw;

  // flags section
  using common_header_view_base::get_flags_raw;
  using common_header_view_base::get_flags;
  using common_header_view_base::get_readable_flags;
  using common_header_view_base::set_flags_raw;
  // clang-format on
};

class [[nodiscard]] common_header_view : private common_header_view_base {
public:
  explicit common_header_view(util::const_byte_span portion)
      : common_header_view_base{util::byte_span{
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
            const_cast<std::byte *>(std::data(portion)), std::size(portion)}} {}

  // deliberately implicit to allow seamless convertion from
  // common_header_updatable_view to common_header_view
  // NOLINTNEXTLINE(hicpp-explicit-conversions)
  common_header_view(const common_header_updatable_view &other)
      : common_header_view_base{other} {}

  // clang-format off
  // timestamp section
  using common_header_view_base::get_timestamp_raw;
  using common_header_view_base::get_timestamp;
  using common_header_view_base::get_readable_timestamp;

  // type_code section
  using common_header_view_base::get_type_code_raw;
  using common_header_view_base::get_type_code;
  using common_header_view_base::get_readable_type_code;

  // server_id section
  using common_header_view_base::get_server_id_raw;

  // event_size section
  using common_header_view_base::get_event_size_raw;

  // next_event_position section
  using common_header_view_base::get_next_event_position_raw;

  // flags section
  using common_header_view_base::get_flags_raw;
  using common_header_view_base::get_flags;
  using common_header_view_base::get_readable_flags;
  // clang-format on
};

} // namespace binsrv::events

#endif // BINSRV_EVENTS_COMMON_HEADER_VIEW_HPP
