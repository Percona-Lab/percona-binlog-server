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

#ifndef BINSRV_EVENT_CHECKSUM_ALGORITHM_TYPE_HPP
#define BINSRV_EVENT_CHECKSUM_ALGORITHM_TYPE_HPP

#include "binsrv/event/checksum_algorithm_type_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace binsrv::event {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// Checksum algorithm type codes copied from
// https://github.com/mysql/mysql-server/blob/mysql-8.0.43/libbinlogevents/include/binlog_event.h#L440
// https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/binlog_event.h#L462
// clang-format off
#define BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_SEQUENCE() \
  BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO(off  ,  0), \
  BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO(crc32,  1)
// clang-format on

#define BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO(X, Y) X = Y
// NOLINTNEXTLINE(readability-enum-initial-value,cert-int09-c)
enum class checksum_algorithm_type : std::uint8_t {
  BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_SEQUENCE(),
  delimiter
};
#undef BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO

inline std::string_view to_string_view(checksum_algorithm_type code) noexcept {
  using namespace std::string_view_literals;
  using nv_pair = std::pair<checksum_algorithm_type, std::string_view>;
#define BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO(X, Y)                          \
  nv_pair { checksum_algorithm_type::X, #X##sv }
  static constexpr std::array labels{
      BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_SEQUENCE(),
      nv_pair{checksum_algorithm_type::delimiter, ""sv}};
#undef BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO
  // NOLINTNEXTLINE(llvm-qualified-auto,readability-qualified-auto)
  const auto fnd{std::ranges::find(labels, code, &nv_pair::first)};
  return fnd == std::end(labels) ? ""sv : fnd->second;
}
#undef BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_SEQUENCE
// NOLINTEND(cppcoreguidelines-macro-usage)

} // namespace binsrv::event

#endif // BINSRV_EVENT_CHECKSUM_ALGORITHM_TYPE_HPP
