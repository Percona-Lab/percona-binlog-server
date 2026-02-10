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

#ifndef BINSRV_CTIME_TIMESTAMP_RANGE_HPP
#define BINSRV_CTIME_TIMESTAMP_RANGE_HPP

#include "binsrv/ctime_timestamp_range_fwd.hpp" // IWYU pragma: export

#include <ctime>

#include "binsrv/ctime_timestamp.hpp"

namespace binsrv {

class [[nodiscard]] ctime_timestamp_range {
public:
  ctime_timestamp_range() = default;

  ctime_timestamp_range(const ctime_timestamp &min_timestamp,
                        const ctime_timestamp &max_timestamp) noexcept;

  [[nodiscard]] bool is_empty() const noexcept {
    return min_timestamp_.is_epoch();
  }
  [[nodiscard]] const ctime_timestamp &get_min_timestamp() const noexcept {
    return min_timestamp_;
  }
  [[nodiscard]] const ctime_timestamp &get_max_timestamp() const noexcept {
    return max_timestamp_;
  }

  void clear() noexcept {
    min_timestamp_.reset_to_epoch();
    max_timestamp_.reset_to_epoch();
  }

  void add_timestamp(const ctime_timestamp &timestamp) noexcept;

  void add_range(const ctime_timestamp_range &range) noexcept;

private:
  ctime_timestamp min_timestamp_{};
  ctime_timestamp max_timestamp_{};
};

} // namespace binsrv

#endif // BINSRV_CTIME_TIMESTAMP_RANGE_HPP
