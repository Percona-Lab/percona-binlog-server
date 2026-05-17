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

#include "util/timestamp_helpers.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>

#include <boost/date_time/posix_time/conversion.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/posix_time/time_formatters_limited.hpp>

namespace util {

[[nodiscard]] std::uint64_t high_resolution_time_point_to_microseconds(
    const high_resolution_time_point &timestamp) noexcept {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          timestamp.time_since_epoch())
          .count());
}

[[nodiscard]] high_resolution_time_point
microseconds_to_high_resolution_time_point(
    std::uint64_t microseconds) noexcept {
  return high_resolution_time_point{std::chrono::microseconds{microseconds}};
}

[[nodiscard]] std::string
microseconds_to_iso_extended_string(std::uint64_t microseconds) {
  // there is still no way to get string representation of the
  // std::chrono::high_resolution_clock::time_point using standard stdlib means,
  // so using boost::posix_time::ptime here

  // TODO: consider switching to std::format()
  boost::posix_time::ptime timestamp{
      boost::posix_time::from_time_t(std::time_t{})};
  timestamp += boost::posix_time::microseconds{microseconds};
  return boost::posix_time::to_iso_extended_string(timestamp);
}

} // namespace util
