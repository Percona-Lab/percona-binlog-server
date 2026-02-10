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

#ifndef BINSRV_CTIME_TIMESTAMP_HPP
#define BINSRV_CTIME_TIMESTAMP_HPP

#include "binsrv/ctime_timestamp_fwd.hpp" // IWYU pragma: export

#include <ctime>
#include <istream>
#include <ostream>
#include <string_view>

namespace binsrv {

class [[nodiscard]] ctime_timestamp {
public:
  ctime_timestamp() noexcept = default;
  explicit ctime_timestamp(std::time_t value) noexcept : value_{value} {}
  explicit ctime_timestamp(std::string_view value_sv);

  [[nodiscard]] static bool try_parse(std::string_view value_sv,
                                      ctime_timestamp &timestamp) noexcept;

  [[nodiscard]] bool is_epoch() const noexcept {
    return value_ == std::time_t{};
  }
  void reset_to_epoch() noexcept { value_ = std::time_t{}; }

  [[nodiscard]] std::time_t get_value() const noexcept { return value_; }
  [[nodiscard]] std::string str() const;

  friend auto operator<=>(const ctime_timestamp &first,
                          const ctime_timestamp &second) = default;

private:
  std::time_t value_{};
};

} // namespace binsrv

#endif // BINSRV_CTIME_TIMESTAMP_HPP
