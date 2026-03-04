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

#ifndef BINSRV_EVENTS_FORMAT_DESCRIPTION_BODY_IMPL_HPP
#define BINSRV_EVENTS_FORMAT_DESCRIPTION_BODY_IMPL_HPP

#include "binsrv/events/format_description_body_impl_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "binsrv/events/checksum_algorithm_type_fwd.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/conversion_helpers.hpp"

namespace binsrv::events {

template <>
class [[nodiscard]] generic_body_impl<code_type::format_description> {
public:
  static constexpr std::size_t size_in_bytes{1U};

  explicit generic_body_impl(
      checksum_algorithm_type checksum_algorithm) noexcept;
  explicit generic_body_impl(util::const_byte_span portion);

  [[nodiscard]] std::uint8_t get_checksum_algorithm_raw() const noexcept {
    return checksum_algorithm_;
  }
  [[nodiscard]] checksum_algorithm_type
  get_checksum_algorithm() const noexcept {
    return util::index_to_enum<checksum_algorithm_type>(
        get_checksum_algorithm_raw());
  }
  [[nodiscard]] std::string_view
  get_readable_checksum_algorithm() const noexcept;

  [[nodiscard]] bool has_checksum_algorithm() const noexcept;

  [[nodiscard]] static std::size_t calculate_encoded_size() noexcept {
    return size_in_bytes;
  }
  void encode_to(util::byte_span &destination) const;

  friend bool operator==(const generic_body_impl &first,
                         const generic_body_impl &second) = default;

private:
  std::uint8_t checksum_algorithm_{};
};

} // namespace binsrv::events

#endif // BINSRV_EVENTS_FORMAT_DESCRIPTION_BODY_IMPL_HPP
