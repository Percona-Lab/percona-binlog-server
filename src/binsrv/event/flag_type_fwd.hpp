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

#ifndef BINSRV_EVENT_FLAG_TYPE_FWD_HPP
#define BINSRV_EVENT_FLAG_TYPE_FWD_HPP

#include <cstdint>

#include "util/flag_set_fwd.hpp"

namespace binsrv::event {

// NOLINTNEXTLINE(readability-enum-initial-value,cert-int09-c)
enum class flag_type : std::uint16_t;

using flag_set = util::flag_set<flag_type>;

} // namespace binsrv::event

#endif // BINSRV_EVENT_FLAG_TYPE_FWD_HPP
