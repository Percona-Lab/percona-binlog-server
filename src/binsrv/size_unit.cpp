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

#include "binsrv/size_unit.hpp"

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

size_unit::size_unit(std::string_view value_sv)
    : base_value_{}, shift_index_{0U} {
  const char *value_ptr{std::data(value_sv)};
  if (value_ptr == nullptr) {
    util::exception_location().raise<std::invalid_argument>(
        "unable to construct size unit from nullptr");
  }
  const char *value_en{std::next(value_ptr, std::ssize(value_sv))};
  const auto [parse_ptr,
              parse_error]{std::from_chars(value_ptr, value_en, base_value_)};
  if (parse_error != std::errc{}) {
    util::exception_location().raise<std::invalid_argument>(
        "unable to parse numeric part in the size unit");
  }
  if (parse_ptr == value_en) {
    // no unit prefix specified, leave value as is
    return;
  }
  if (std::next(parse_ptr) != value_en) {
    util::exception_location().raise<std::invalid_argument>(
        "unit prefix in the size unit is expected to be a single character");
  }

  const auto *const shifts_bg{std::cbegin(shifts)};
  const auto *const shifts_en{std::cend(shifts)};

  // deliberately skipping the very first element
  const auto *const shifts_it{
      std::find_if(std::next(shifts_bg), shifts_en,
                   [symbol = *parse_ptr](const auto &shift_pair) {
                     return shift_pair.first == symbol;
                   })};
  if (shifts_it == shifts_en) {
    util::exception_location().raise<std::invalid_argument>(
        "unknown unit prefix in the size unit");
  }

  if (base_value_ == 0ULL) {
    // Zero with any multipliyer is still zero, leaving shift_index as zero as
    // well.
    return;
  }

  if (base_value_ > (std::numeric_limits<decltype(base_value_)>::max() >>
                     shifts_it->second)) {
    util::exception_location().raise<std::out_of_range>(
        "the specified size unit cannot be represented in a 64-bit unsigned "
        "integer");
  }

  shift_index_ = static_cast<std::size_t>(std::distance(shifts_bg, shifts_it));
}

} // namespace binsrv
