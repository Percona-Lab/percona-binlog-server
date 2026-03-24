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

#include "util/ctime_timestamp.hpp"

#include <ctime>
#include <exception>
#include <ios>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>

#include <boost/date_time/posix_time/conversion.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>
#include <boost/date_time/posix_time/time_parsers.hpp>

namespace util {

ctime_timestamp::ctime_timestamp(std::string_view value_sv)
    : value_{boost::posix_time::to_time_t(
          boost::posix_time::from_iso_extended_string(std::string{value_sv}))} {
}

[[nodiscard]] bool
ctime_timestamp::try_parse(std::string_view value_sv,
                           ctime_timestamp &timestamp) noexcept {
  bool result{true};
  try {
    timestamp = ctime_timestamp{value_sv};
  } catch (const std::exception &) {
    result = false;
  }
  return result;
}

[[nodiscard]] std::string ctime_timestamp::iso_extended_str() const {
  return boost::posix_time::to_iso_extended_string(
      boost::posix_time::from_time_t(get_value()));
}

[[nodiscard]] std::string ctime_timestamp::simple_str() const {
  return boost::posix_time::to_simple_string(
      boost::posix_time::from_time_t(get_value()));
}

[[nodiscard]] ctime_timestamp ctime_timestamp::now() noexcept {
  return ctime_timestamp{std::time(nullptr)};
}

std::ostream &operator<<(std::ostream &output,
                         const ctime_timestamp &timestamp) {
  return output << timestamp.iso_extended_str();
}

std::istream &operator>>(std::istream &input, ctime_timestamp &timestamp) {
  std::string timestamp_str;
  input >> timestamp_str;
  if (!input) {
    return input;
  }
  try {
    timestamp = ctime_timestamp{timestamp_str};
  } catch (const std::exception &) {
    input.setstate(std::ios_base::failbit);
  }
  return input;
}

} // namespace util
