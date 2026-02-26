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

#ifndef BINSRV_EVENTS_FOOTER_VIEW_HPP
#define BINSRV_EVENTS_FOOTER_VIEW_HPP

#include "binsrv/events/footer_view_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <string>

#include "binsrv/events/protocol_traits_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::events {

class footer_view_base {
public:
  static constexpr std::size_t crc_offset{0U};
  static constexpr std::size_t size_in_bytes{crc_offset +
                                             sizeof(std::uint32_t)};
  static_assert(size_in_bytes == default_footer_length);

  [[nodiscard]] static std::string get_readable_crc_from_raw(std::uint32_t crc);

protected:
  explicit footer_view_base(util::byte_span portion);

  // crc section
  [[nodiscard]] std::uint32_t get_crc_raw() const noexcept;
  [[nodiscard]] std::string get_readable_crc() const {
    return get_readable_crc_from_raw(get_crc_raw());
  }

  void set_crc_raw(std::uint32_t crc) const noexcept;

private:
  util::byte_span portion_{};
};

class [[nodiscard]] footer_updatable_view : private footer_view_base {
  friend class footer_view;

public:
  explicit footer_updatable_view(util::byte_span portion)
      : footer_view_base{portion} {}

  // clang-format off
  // crc section
  using footer_view_base::get_crc_raw;
  using footer_view_base::get_readable_crc;

  using footer_view_base::set_crc_raw;
  // clang-format on
};

class [[nodiscard]] footer_view : private footer_view_base {
public:
  explicit footer_view(util::const_byte_span portion)
      : footer_view_base{util::byte_span{
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
            const_cast<std::byte *>(std::data(portion)), std::size(portion)}} {}

  // deliberately implicit to allow seamless convertion from
  // footer_updatable_view to footer_view
  // NOLINTNEXTLINE(hicpp-explicit-conversions)
  footer_view(const footer_updatable_view &other) : footer_view_base{other} {}

  // clang-format off
  // crc section
  using footer_view_base::get_crc_raw;
  using footer_view_base::get_readable_crc;
  // clang-format on
};

} // namespace binsrv::events

#endif // BINSRV_EVENTS_FOOTER_VIEW_HPP
