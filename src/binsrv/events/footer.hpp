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

#ifndef BINSRV_EVENTS_FOOTER_HPP
#define BINSRV_EVENTS_FOOTER_HPP

#include "binsrv/events/footer_fwd.hpp" // IWYU pragma: export

#include <cstdint>

#include "binsrv/events/footer_view.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::events {

class [[nodiscard]] footer {
public:
  static constexpr std::size_t size_in_bytes{footer_view_base::size_in_bytes};

  explicit footer(std::uint32_t crc) noexcept : crc_{crc} {}

  explicit footer(const footer_view &view);
  explicit footer(util::const_byte_span portion);

  [[nodiscard]] std::uint32_t get_crc_raw() const noexcept { return crc_; }
  [[nodiscard]] std::string get_readable_crc() const {
    return footer_view_base::get_readable_crc_from_raw(get_crc_raw());
  }

  [[nodiscard]] static std::size_t calculate_encoded_size() noexcept {
    return size_in_bytes;
  }
  void encode_to(util::byte_span &destination) const;

  friend bool operator==(const footer &first, const footer &second) = default;

private:
  std::uint32_t crc_{};
};

} // namespace binsrv::events

#endif // BINSRV_EVENTS_FOOTER_HPP
