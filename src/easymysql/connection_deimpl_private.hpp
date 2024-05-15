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

#ifndef EASYMYSQL_CONNECTION_DEIMPL_PRIVATE_HPP
#define EASYMYSQL_CONNECTION_DEIMPL_PRIVATE_HPP

#include <mysql/mysql.h>

#include "util/impl_helpers.hpp"

namespace easymysql {

using mysql_deimpl = util::deimpl<MYSQL>;
using rpl_deimpl = util::deimpl<MYSQL_RPL>;

} // namespace easymysql

#endif // EASYMYSQL_CONNECTION_DEIMPL_PRIVATE_HPP
