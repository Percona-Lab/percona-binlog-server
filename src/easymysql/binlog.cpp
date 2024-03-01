#include "easymysql/binlog.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include <mysql/mysql.h>

#include "easymysql/binlog_deimpl_private.hpp"
#include "easymysql/connection.hpp"
#include "easymysql/connection_deimpl_private.hpp"
#include "easymysql/core_error_helpers_private.hpp"

#include "util/byte_span_fwd.hpp"

namespace easymysql {

void binlog::rpl_deleter::operator()(void *ptr) const noexcept {
  if (ptr != nullptr) {
    // deleting via std::default_delete to avoid
    // cppcoreguidelines-owning-memory warnings
    using delete_helper = std::default_delete<MYSQL_RPL>;
    delete_helper{}(static_cast<MYSQL_RPL *>(ptr));
  }
}

binlog::binlog(connection &conn, std::uint32_t server_id,
               std::string_view file_name, std::uint64_t position)
    : conn_{&conn},
      impl_{new MYSQL_RPL{.file_name_length = std::size(file_name),
                          .file_name = std::data(file_name),
                          .start_position = position,
                          .server_id = server_id,
                          // TODO: consider adding (or-ing)
                          // BINLOG_DUMP_NON_BLOCK and
                          // MYSQL_RPL_SKIP_HEARTBEAT to flags
                          .flags = 0U,
                          .gtid_set_encoded_size = 0U,
                          .fix_gtid_set = nullptr,
                          .gtid_set_arg = nullptr,
                          .size = 0U,
                          .buffer = nullptr

      }} {
  assert(!conn.is_empty());

  // WL#2540: Replication event checksums
  // https://dev.mysql.com/worklog/task/?id=2540
  static constexpr std::string_view crc_query{
      "SET @source_binlog_checksum = 'NONE', "
      "@master_binlog_checksum = 'NONE'"};
  conn_->execute_generic_query_noresult(crc_query);

  auto *casted_conn_impl = connection_deimpl::get(conn.impl_);
  if (mysql_binlog_open(casted_conn_impl, binlog_deimpl::get(impl_)) != 0) {
    raise_core_error_from_connection(conn);
  }
}

binlog::~binlog() {
  if (conn_ != nullptr) {
    assert(!conn_->is_empty());
    assert(!is_empty());
    mysql_binlog_close(connection_deimpl::get(conn_->impl_),
                       binlog_deimpl::get(impl_));
  }
}

util::const_byte_span binlog::fetch() {
  assert(conn_ != nullptr);
  assert(!is_empty());
  auto *casted_conn_impl = connection_deimpl::get(conn_->impl_);
  auto *casted_rpl_impl = binlog_deimpl::get(impl_);
  if (mysql_binlog_fetch(casted_conn_impl, casted_rpl_impl) != 0) {
    raise_core_error_from_connection(*conn_);
  }
  return std::as_bytes(
      std::span{casted_rpl_impl->buffer, casted_rpl_impl->size});
}

} // namespace easymysql
