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

#ifndef BINSRV_GTID_COMMON_TYPES_HPP
#define BINSRV_GTID_COMMON_TYPES_HPP

#include <cstdint>
#include <limits>

#include <boost/uuid/uuid.hpp>

namespace binsrv::gtid {

using gno_t = std::int64_t;
inline constexpr gno_t min_gno{1LL};
inline constexpr gno_t max_gno{std::numeric_limits<gno_t>::max()};

using uuid = boost::uuids::uuid;

} // namespace binsrv::gtid

#endif // BINSRV_GTID_COMMON_TYPES_HPP
