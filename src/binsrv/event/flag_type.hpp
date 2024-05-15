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

#ifndef BINSRV_EVENT_FLAG_TYPE_HPP
#define BINSRV_EVENT_FLAG_TYPE_HPP

#include "binsrv/event/flag_type_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include "util/flag_set.hpp"

namespace binsrv::event {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// Event flags copied from
// https://github.com/mysql/mysql-server/blob/mysql-8.0.37/sql/log_event.h#L247
// 'binlog_in_use' flag is copied from
// https://github.com/mysql/mysql-server/blob/mysql-8.0.37/libbinlogevents/include/binlog_event.h#L270
// This flag is used as a marker in the common header section of the very
// first format description event that this particular binlog is currently in
// use. It us cleared(rewritten) by the server when the binary log is
// closed).
// Events received via network stream should never have this flag set.
// clang-format off
#define BINSRV_EVENT_FLAG_TYPE_XY_SEQUENCE() \
  BINSRV_EVENT_FLAG_TYPE_XY_MACRO(binlog_in_use  , 0x001U), \
  BINSRV_EVENT_FLAG_TYPE_XY_MACRO(thread_specific, 0x004U), \
  BINSRV_EVENT_FLAG_TYPE_XY_MACRO(suppress_use   , 0x008U), \
  BINSRV_EVENT_FLAG_TYPE_XY_MACRO(artificial     , 0x020U), \
  BINSRV_EVENT_FLAG_TYPE_XY_MACRO(relay_log      , 0x040U), \
  BINSRV_EVENT_FLAG_TYPE_XY_MACRO(ignorable      , 0x080U), \
  BINSRV_EVENT_FLAG_TYPE_XY_MACRO(no_filter      , 0x100U), \
  BINSRV_EVENT_FLAG_TYPE_XY_MACRO(mts_isolate    , 0x200U)
// clang-format on

#define BINSRV_EVENT_FLAG_TYPE_XY_MACRO(X, Y) X = Y
enum class flag_type : std::uint16_t {
  BINSRV_EVENT_FLAG_TYPE_XY_SEQUENCE(),
  delimiter
};
#undef BINSRV_EVENT_FLAG_TYPE_XY_MACRO

inline std::string_view to_string_view(flag_type code) noexcept {
  using namespace std::string_view_literals;
  using nv_pair = std::pair<flag_type, std::string_view>;
#define BINSRV_EVENT_FLAG_TYPE_XY_MACRO(X, Y)                                  \
  nv_pair { flag_type::X, #X##sv }
  static constexpr std::array labels{BINSRV_EVENT_FLAG_TYPE_XY_SEQUENCE(),
                                     nv_pair{flag_type::delimiter, ""sv}};
#undef BINSRV_EVENT_FLAG_TYPE_XY_MACRO
  // NOLINTNEXTLINE(llvm-qualified-auto,readability-qualified-auto)
  const auto fnd{std::ranges::find(labels, code, &nv_pair::first)};
  return fnd == std::end(labels) ? ""sv : fnd->second;
}
#undef BINSRV_EVENT_FLAG_TYPE_XY_SEQUENCE
// NOLINTEND(cppcoreguidelines-macro-usage)

} // namespace binsrv::event

#endif // BINSRV_EVENT_FLAG_TYPE_HPP
