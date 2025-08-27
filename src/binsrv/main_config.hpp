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

#ifndef BINSRV_MAIN_CONFIG_HPP
#define BINSRV_MAIN_CONFIG_HPP

#include "binsrv/main_config_fwd.hpp" // IWYU pragma: export

#include "binsrv/logger_config.hpp"  // IWYU pragma: export
#include "binsrv/storage_config.hpp" // IWYU pragma: export

#include "easymysql/connection_config.hpp"  // IWYU pragma: export
#include "easymysql/replication_config.hpp" // IWYU pragma: export

#include "util/nv_tuple.hpp"

namespace binsrv {

class [[nodiscard]] main_config {
private:
  using impl_type = util::nv_tuple<
      // clang-format off
      util::nv<"logger"     , logger_config>,
      util::nv<"connection" , easymysql::connection_config>,
      util::nv<"replication", easymysql::replication_config>,
      util::nv<"storage"    , storage_config>
      // clang-format on
      >;

public:
  explicit main_config(std::string_view file_name);

  [[nodiscard]] const auto &root() const noexcept { return impl_; }

private:
  impl_type impl_;
};

} // namespace binsrv

#endif // BINSRV_MAIN_CONFIG_HPP
