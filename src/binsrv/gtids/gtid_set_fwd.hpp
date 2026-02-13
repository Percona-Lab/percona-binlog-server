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

#ifndef BINSRV_GTIDS_GTID_SET_FWD_HPP
#define BINSRV_GTIDS_GTID_SET_FWD_HPP

#include <iosfwd>
#include <optional>

#include "util/nv_tuple_json_support.hpp"

namespace binsrv::gtids {

class gtid_set;
using optional_gtid_set = std::optional<gtid_set>;

std::ostream &operator<<(std::ostream &output, const gtid_set &obj);
std::istream &operator>>(std::istream &input, gtid_set &obj);

} // namespace binsrv::gtids

template <>
struct util::is_string_convertable<binsrv::gtids::gtid_set> : std::true_type {};

#endif // BINSRV_GTIDS_GTID_SET_FWD_HPP
