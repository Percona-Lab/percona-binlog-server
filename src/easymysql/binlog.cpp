#include "easymysql/binlog.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string_view>

#include <mysql/mysql.h>

#include "easymysql/binlog_deimpl_private.hpp"
#include "easymysql/connection.hpp"
#include "easymysql/connection_deimpl_private.hpp"
#include "easymysql/core_error_helpers_private.hpp"

namespace {

constexpr std::uint64_t magic_binlog_offset = 4ULL;

} // anonymous namespace

namespace easymysql {

void binlog::rpl_deleter::operator()(void *ptr) const noexcept {
  if (ptr != nullptr) {
    // deleting via std::default_delete to avoid
    // cppcoreguidelines-owning-memory warnings
    using delete_helper = std::default_delete<MYSQL_RPL>;
    delete_helper{}(static_cast<MYSQL_RPL *>(ptr));
  }
}

binlog::binlog(connection &conn, std::uint32_t server_id)
    : conn_{&conn}, impl_{new MYSQL_RPL{.file_name_length = 0,
                                        .file_name = nullptr,
                                        .start_position = magic_binlog_offset,
                                        .server_id = server_id,
                                        // TODO: consider adding (or-ing)
                                        // BINLOG_DUMP_NON_BLOCK and
                                        // MYSQL_RPL_SKIP_HEARTBEAT to flags
                                        .flags = 0,
                                        .gtid_set_encoded_size = 0,
                                        .fix_gtid_set = nullptr,
                                        .gtid_set_arg = nullptr,
                                        .size = 0,
                                        .buffer = nullptr

                    }} {
  assert(!conn.is_empty());

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

binlog_stream_span binlog::fetch() {
  assert(conn_ != nullptr);
  assert(!is_empty());
  auto *casted_conn_impl = connection_deimpl::get(conn_->impl_);
  auto *casted_rpl_impl = binlog_deimpl::get(impl_);
  if (mysql_binlog_fetch(casted_conn_impl, casted_rpl_impl) != 0) {
    raise_core_error_from_connection(*conn_);
  }
  return {casted_rpl_impl->buffer, casted_rpl_impl->size};
}

} // namespace easymysql
