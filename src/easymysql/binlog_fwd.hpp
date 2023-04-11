#ifndef EASYMYSQL_BINLOG_FWD_HPP
#define EASYMYSQL_BINLOG_FWD_HPP

#include <span>

namespace easymysql {

class binlog;

using binlog_stream_span = std::span<const unsigned char>;

} // namespace easymysql

#endif // EASYMYSQL_BINLOG_FWD_HPP
