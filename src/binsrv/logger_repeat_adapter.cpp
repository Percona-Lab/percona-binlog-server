// Copyright (c) 2026 Percona and/or its affiliates.
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

#include "binsrv/logger_repeat_adapter.hpp"

#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <utility>

#include "binsrv/basic_logger_fwd.hpp"

#include "util/exception_location_helpers.hpp"

namespace binsrv {

logger_repeat_adapter::logger_repeat_adapter(basic_logger_ptr logger)
    : logger_{std::move(logger)} {
  if (!logger_) {
    util::exception_location().raise<std::invalid_argument>(
        "logger repeat adapter requires a non-null logger");
  }
}

std::size_t logger_repeat_adapter::reset_group(std::size_t group_id) {
  if (group_id >= std::size(counters_)) {
    return 0UZ;
  }
  return std::exchange(counters_[group_id], 0UZ);
}

[[nodiscard]] bool
logger_repeat_adapter::register_repeat(std::size_t group_id) {
  if (group_id >= std::size(counters_)) {
    counters_.resize(group_id + 1UZ, 0UZ);
  }
  return counters_[group_id]++ == 0UZ;
}

} // namespace binsrv
