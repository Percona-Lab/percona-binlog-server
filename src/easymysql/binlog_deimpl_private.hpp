#ifndef EASYMYSQL_BINLOG_DEIMPL_PRIVATE_HPP
#define EASYMYSQL_BINLOG_DEIMPL_PRIVATE_HPP

#include <mysql/mysql.h>

#include "util/impl_helpers.hpp"

namespace easymysql {

using binlog_deimpl = util::deimpl<MYSQL_RPL>;

} // namespace easymysql

#endif // EASYMYSQL_BINLOG_DEIMPL_PRIVATE_HPP
