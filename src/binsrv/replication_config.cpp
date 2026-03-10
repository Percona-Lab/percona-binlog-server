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

#include "binsrv/replication_config.hpp"

#include <stdexcept>

#include "binsrv/replication_mode_type.hpp"

#include "util/exception_location_helpers.hpp"

namespace binsrv {

void replication_config::validate() const {
  const auto &optional_rewrite{get<"rewrite">()};
  if (optional_rewrite.has_value()) {
    if (get<"mode">() != replication_mode_type::gtid) {
      util::exception_location().raise<std::invalid_argument>(
          "error validating replication config: "
          "rewrite can only be enabled in gtid replication mode");
    }

    optional_rewrite->validate();
  }
}

} // namespace binsrv
