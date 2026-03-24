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

#include "util/ctime_timestamp_range.hpp"

#include <algorithm>

#include "util/ctime_timestamp.hpp"

namespace util {

ctime_timestamp_range::ctime_timestamp_range(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const ctime_timestamp &min_timestamp,
    const ctime_timestamp &max_timestamp) noexcept
    : min_timestamp_{min_timestamp}, max_timestamp_{max_timestamp} {
  if (min_timestamp_.is_epoch() && !max_timestamp_.is_epoch()) {
    min_timestamp_ = max_timestamp_;
  }
  if (!min_timestamp_.is_epoch() && max_timestamp_.is_epoch()) {
    max_timestamp_ = min_timestamp_;
  }
}

void ctime_timestamp_range::add_timestamp(
    const ctime_timestamp &timestamp) noexcept {
  if (timestamp.is_epoch()) {
    return;
  }
  if (is_empty()) {
    min_timestamp_ = timestamp;
    max_timestamp_ = timestamp;
    return;
  }
  min_timestamp_ = std::min(min_timestamp_, timestamp);
  max_timestamp_ = std::max(max_timestamp_, timestamp);
}

void ctime_timestamp_range::add_range(
    const ctime_timestamp_range &range) noexcept {
  if (range.is_empty()) {
    return;
  }
  if (is_empty()) {
    *this = range;
    return;
  }
  min_timestamp_ = std::min(min_timestamp_, range.get_min_timestamp());
  max_timestamp_ = std::max(max_timestamp_, range.get_max_timestamp());
}

} // namespace util
