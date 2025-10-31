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

#ifndef BINSRV_EVENT_GTID_LOG_FLAG_TYPE_HPP
#define BINSRV_EVENT_GTID_LOG_FLAG_TYPE_HPP

#include "binsrv/event/gtid_log_flag_type_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include "util/flag_set.hpp"

namespace binsrv::event {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// GTID_LOG event flags copied from
// https://github.com/mysql/mysql-server/blob/mysql-8.0.43/libbinlogevents/include/control_events.h#L947
// https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.h#L956
//
// https://github.com/mysql/mysql-server/blob/mysql-8.0.43/libbinlogevents/include/control_events.h#L1017
// https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.h#L1027
// 00000001 = Transaction may have changes logged with SBR
// clang-format off
#define BINSRV_EVENT_GTID_LOG_FLAG_TYPE_XY_SEQUENCE() \
  BINSRV_EVENT_GTID_LOG_FLAG_TYPE_XY_MACRO(may_have_sbr, 0x01U)
// clang-format on

#define BINSRV_EVENT_GTID_LOG_FLAG_TYPE_XY_MACRO(X, Y) X = Y
// NOLINTNEXTLINE(readability-enum-initial-value,cert-int09-c)
enum class gtid_log_flag_type : std::uint8_t {
  BINSRV_EVENT_GTID_LOG_FLAG_TYPE_XY_SEQUENCE(),
  delimiter
};
#undef BINSRV_EVENT_GTID_LOG_FLAG_TYPE_XY_MACRO

inline std::string_view to_string_view(gtid_log_flag_type code) noexcept {
  using namespace std::string_view_literals;
  using nv_pair = std::pair<gtid_log_flag_type, std::string_view>;
#define BINSRV_EVENT_GTID_LOG_FLAG_TYPE_XY_MACRO(X, Y)                         \
  nv_pair { gtid_log_flag_type::X, #X##sv }
  static constexpr std::array labels{
      BINSRV_EVENT_GTID_LOG_FLAG_TYPE_XY_SEQUENCE(),
      nv_pair{gtid_log_flag_type::delimiter, ""sv}};
#undef BINSRV_EVENT_GTID_LOG_FLAG_TYPE_XY_MACRO
  // NOLINTNEXTLINE(llvm-qualified-auto,readability-qualified-auto)
  const auto fnd{std::ranges::find(labels, code, &nv_pair::first)};
  return fnd == std::end(labels) ? ""sv : fnd->second;
}
#undef BINSRV_EVENT_GTID_LOG_FLAG_TYPE_XY_SEQUENCE
// NOLINTEND(cppcoreguidelines-macro-usage)

} // namespace binsrv::event

#endif // BINSRV_EVENT_GTID_LOG_FLAG_TYPE_HPP
