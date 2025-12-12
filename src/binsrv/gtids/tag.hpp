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

#ifndef BINSRV_GTIDS_TAG_HPP
#define BINSRV_GTIDS_TAG_HPP

#include "binsrv/gtids/tag_fwd.hpp" // IWYU pragma: export

#include <string_view>

#include "binsrv/gtids/common_types.hpp"

#include "util/bounded_string_storage.hpp"
#include "util/byte_span.hpp"

namespace binsrv::gtids {

class tag {
public:
  tag() noexcept = default;

  explicit tag(std::string_view name);

  explicit tag(const tag_storage &data) : tag(util::as_string_view(data)) {}

  [[nodiscard]] std::string_view get_name() const noexcept {
    return util::as_string_view(data_);
  }

  [[nodiscard]] bool is_empty() const noexcept { return data_.empty(); }

  [[nodiscard]] std::size_t get_size() const noexcept {
    return std::size(data_);
  }

  [[nodiscard]] std::size_t calculate_encoded_size() const noexcept;
  void encode_to(util::byte_span &destination) const;

private:
  tag_storage data_{};
};

[[nodiscard]] inline auto operator<=>(const tag &first,
                                      const tag &second) noexcept {
  return first.get_name() <=> second.get_name();
}
[[nodiscard]] inline auto operator==(const tag &first,
                                     const tag &second) noexcept {
  return first.get_name() == second.get_name();
}

} // namespace binsrv::gtids

#endif // BINSRV_GTIDS_TAG_HPP
