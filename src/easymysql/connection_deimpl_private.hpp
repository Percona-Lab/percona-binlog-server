#ifndef EASYMYSQL_CONNECTION_DEIMPL_PRIVATE_HPP
#define EASYMYSQL_CONNECTION_DEIMPL_PRIVATE_HPP

#include <mysql/mysql.h>

#include "util/impl_helpers.hpp"

namespace easymysql {

using connection_deimpl = util::deimpl<MYSQL>;

} // namespace easymysql

#endif // EASYMYSQL_CONNECTION_DEIMPL_PRIVATE_HPP
