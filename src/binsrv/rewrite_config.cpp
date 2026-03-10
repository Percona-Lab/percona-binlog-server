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

#include "binsrv/rewrite_config.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

#include "util/exception_location_helpers.hpp"

namespace binsrv {

void rewrite_config::validate() const {
  static constexpr std::uint64_t min_file_size{1024ULL};
  if (get<"file_size">().get_value() < min_file_size) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating rewrite config: file size must be >= " +
        std::to_string(min_file_size) + " bytes");
  }
}

} // namespace binsrv
