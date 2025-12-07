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

#ifndef BINSRV_STORAGE_METADATA_HPP
#define BINSRV_STORAGE_METADATA_HPP

#include "binsrv/storage_metadata_fwd.hpp" // IWYU pragma: export

#include <string>
#include <string_view>

#include "binsrv/replication_mode_type_fwd.hpp"

#include "util/nv_tuple.hpp"

namespace binsrv {

class [[nodiscard]] storage_metadata {
private:
  using impl_type = util::nv_tuple<
      // clang-format off
      util::nv<"version", std::uint32_t>,
      util::nv<"mode", replication_mode_type>
      // clang-format on
      >;

public:
  storage_metadata();

  explicit storage_metadata(std::string_view data);

  [[nodiscard]] std::string str() const;

  [[nodiscard]] auto &root() noexcept { return impl_; }
  [[nodiscard]] const auto &root() const noexcept { return impl_; }

private:
  impl_type impl_;

  void validate() const;
};

} // namespace binsrv

#endif // BINSRV_STORAGE_METADATA_HPP
