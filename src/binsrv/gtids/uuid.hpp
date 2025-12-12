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

#ifndef BINSRV_GTIDS_UUID_HPP
#define BINSRV_GTIDS_UUID_HPP

#include "binsrv/gtids/uuid_fwd.hpp" // IWYU pragma: export

#include <string>
#include <string_view>

#include <boost/uuid/uuid.hpp>

#include "binsrv/gtids/common_types.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::gtids {

class uuid {
public:
  uuid() noexcept = default;

  explicit uuid(std::string_view value);

  explicit uuid(const uuid_storage &data);

  [[nodiscard]] bool is_empty() const noexcept { return data_.is_nil(); }

  [[nodiscard]] std::string str() const;

  [[nodiscard]] static std::size_t calculate_encoded_size() noexcept {
    return uuid_length;
  }
  void encode_to(util::byte_span &destination) const;

  [[nodiscard]] friend auto operator<=>(const uuid &first,
                                        const uuid &second) noexcept = default;

private:
  boost::uuids::uuid data_{};
};

} // namespace binsrv::gtids

#endif // BINSRV_GTIDS_UUID_HPP
