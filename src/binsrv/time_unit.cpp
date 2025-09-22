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

#include "binsrv/time_unit.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include "util/exception_location_helpers.hpp"

namespace binsrv {

time_unit::time_unit(std::string_view value_sv)
    : base_value_{}, multiplier_index_{0U} {
  const char *value_ptr{std::data(value_sv)};
  if (value_ptr == nullptr) {
    util::exception_location().raise<std::invalid_argument>(
        "unable to construct time unit from nullptr");
  }
  const char *value_en{std::next(value_ptr, std::ssize(value_sv))};
  const auto [parse_ptr,
              parse_error]{std::from_chars(value_ptr, value_en, base_value_)};
  if (parse_error != std::errc{}) {
    util::exception_location().raise<std::invalid_argument>(
        "unable to parse numeric part in the time unit");
  }
  if (parse_ptr == value_en) {
    // no unit prefix specified, leave value as is
    return;
  }
  if (std::next(parse_ptr) != value_en) {
    util::exception_location().raise<std::invalid_argument>(
        "unit prefix in the time unit is expected to be a single character");
  }

  const auto *const multipliers_bg{std::cbegin(multipliers)};
  const auto *const multipliers_en{std::cend(multipliers)};

  // deliberately skipping the very first element
  const auto *const multipliers_it{
      std::find_if(multipliers_bg, multipliers_en,
                   [symbol = *parse_ptr](const auto &maltiplier_pair) {
                     return maltiplier_pair.first == symbol;
                   })};
  if (multipliers_it == multipliers_en) {
    util::exception_location().raise<std::invalid_argument>(
        "unknown unit prefix in the time unit");
  }

  if (base_value_ == 0ULL) {
    // Zero with any multipliyer is still zero, leaving multiplier_index_ as
    // zero as well.
    return;
  }

  if (base_value_ > (std::numeric_limits<decltype(base_value_)>::max() /
                     multipliers_it->second)) {
    util::exception_location().raise<std::out_of_range>(
        "the specified time unit cannot be represented in a 64-bit unsigned "
        "integer");
  }

  multiplier_index_ =
      static_cast<std::size_t>(std::distance(multipliers_bg, multipliers_it));
}

} // namespace binsrv
