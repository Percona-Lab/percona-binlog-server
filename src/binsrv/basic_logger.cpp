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

#include "binsrv/basic_logger.hpp"

#include <string>
#include <string_view>

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/posix_time/time_formatters_limited.hpp>

#include "binsrv/log_severity.hpp"

namespace binsrv {

basic_logger::basic_logger(log_severity min_level) noexcept
    : min_level_{min_level} {}

void basic_logger::log(log_severity level, std::string_view message) {
  if (level >= min_level_) {
    static constexpr auto timestamp_length{
        std::size("YYYY-MM-DDTHH:MM:SS.fffffffff") - 1U};
    const auto timestamp = boost::posix_time::microsec_clock::universal_time();
    ;
    const auto level_label = to_string_view(level);
    std::string buf;
    buf.reserve(1 + timestamp_length + 1 + 1 + 1 + std::size(level_label) + 1 +
                1 + std::size(message));
    buf += '[';
    buf += boost::posix_time::to_iso_extended_string(timestamp);
    buf += "] [";
    buf += level_label;
    buf += "] ";
    buf += message;
    do_log(buf);
  }
}

} // namespace binsrv
