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

#include "easymysql/connection.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <mysql/errmsg.h>
#include <mysql/mysql.h>

#include "easymysql/connection_config.hpp"
#include "easymysql/connection_deimpl_private.hpp"
#include "easymysql/core_error_helpers_private.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace easymysql {

void connection::mysql_deleter::operator()(void *ptr) const noexcept {
  if (ptr != nullptr) {
    mysql_close(static_cast<MYSQL *>(ptr));
  }
}

class connection::rpl_impl {
public:
  rpl_impl(connection &conn, std::uint32_t server_id,
           std::string_view file_name, std::uint64_t position,
           connection_replication_mode_type blocking_mode)
      : conn_{mysql_deimpl::get(conn.mysql_impl_)},
        rpl_{.file_name_length = std::size(file_name),
             .file_name = std::data(file_name),
             .start_position = position,
             .server_id = server_id,
             .flags = get_rpl_flags(blocking_mode),
             .gtid_set_encoded_size = 0U,
             .fix_gtid_set = nullptr,
             .gtid_set_arg = nullptr,
             .size = 0U,
             .buffer = nullptr} {
    if (mysql_binlog_open(conn_, &rpl_) != 0) {
      raise_core_error_from_connection("cannot open binary log", conn);
    }
  }

  rpl_impl(const rpl_impl &) = delete;
  rpl_impl &operator=(const rpl_impl &) = delete;
  rpl_impl(rpl_impl &&) = delete;
  rpl_impl &operator=(rpl_impl &&) = delete;

  ~rpl_impl() { mysql_binlog_close(conn_, &rpl_); }

  [[nodiscard]] bool fetch(util::const_byte_span &result) {
    if (mysql_binlog_fetch(conn_, &rpl_) != 0) {
      return false;
    }

    result = std::as_bytes(std::span{rpl_.buffer, rpl_.size});
    return true;
  }

private:
  MYSQL *conn_;
  MYSQL_RPL rpl_;

  // MYSQL_RPL_SKIP_HEARTBEAT
  // https://github.com/mysql/mysql-server/blob/mysql-cluster-8.0.37/include/mysql.h#L366

  // USE_HEARTBEAT_EVENT_V2
  // https://github.com/mysql/mysql-server/blob/mysql-cluster-8.0.37/include/mysql.h#L372

  // Explaining BINLOG_DUMP_NON_BLOCK
  // https://github.com/mysql/mysql-server/blob/mysql-8.0.37/sql/rpl_constants.h#L45
  // https://github.com/mysql/mysql-server/blob/mysql-8.0.37/sql/rpl_binlog_sender.cc#L313

  // For some reason BINLOG_DUMP_NON_BLOCK is a private constant, defining is
  // locally
  static constexpr unsigned int private_binlog_dump_non_block{1U};
  [[nodiscard]] static constexpr unsigned int
  get_rpl_flags(connection_replication_mode_type blocking_mode) noexcept {
    return MYSQL_RPL_SKIP_HEARTBEAT |
           (blocking_mode == connection_replication_mode_type::non_blocking
                ? private_binlog_dump_non_block
                : 0U);
  }
};

connection::connection() noexcept = default;

connection::connection(const connection_config &config)
    : mysql_impl_{mysql_init(nullptr)}, rpl_impl_{} {
  if (!mysql_impl_) {
    util::exception_location().raise<std::logic_error>(
        "cannot create MYSQL object");
  }
  auto *casted_impl = mysql_deimpl::get(mysql_impl_);

  const unsigned int connect_timeout{config.get<"connect_timeout">()};
  if (mysql_options(casted_impl, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout) !=
      0) {
    raise_core_error_from_connection("cannot set MySQL connect timeout", *this);
  }
  const unsigned int read_timeout{config.get<"read_timeout">()};
  if (mysql_options(casted_impl, MYSQL_OPT_READ_TIMEOUT, &read_timeout) != 0) {
    raise_core_error_from_connection("cannot set MySQL read timeout", *this);
  }
  const unsigned int write_timeout{config.get<"write_timeout">()};
  if (mysql_options(casted_impl, MYSQL_OPT_WRITE_TIMEOUT, &write_timeout) !=
      0) {
    raise_core_error_from_connection("cannot set MySQL write timeout", *this);
  }

  if (mysql_real_connect(casted_impl,
                         /*        host */ config.get<"host">().c_str(),
                         /*        user */ config.get<"user">().c_str(),
                         /*    password */ config.get<"password">().c_str(),
                         /*          db */ nullptr,
                         /*        port */ config.get<"port">(),
                         /* unix socket */ nullptr,
                         /*       flags */ 0) == nullptr) {
    raise_core_error_from_connection("cannot establish MySQL connection",
                                     *this);
  }
}

// default move constructor is OK as it will never do any
// 'mysql_impl_' / 'rpl_impl_' destruction
connection::connection(connection &&) noexcept = default;

// default move assignment operator, e.g
// {
//   this->mysql_impl_ = std::move(other.mysql_impl_);
//   this->rpl_impl_ = std::move(other.rpl_impl_);
// }
// is not OK as the first statement will destroy the old 'MYSQL*'
// object in 'this->mysql_impl_', which will be needed during
// the destruction of the old 'MYSQL_RPL*' object from
// 'this->rpl_impl_'
connection &connection::operator=(connection &&other) noexcept {
  connection local{std::move(other)};
  swap(local);
  return *this;
}

// default destructor is OK as 'mysql_impl_' and 'rpl_impl_' will be
// destrojed in the order reverse to how they were declared, e.g.
// 'rpl_impl_' first, 'mysql_impl_' second
connection::~connection() = default;

void connection::swap(connection &other) noexcept {
  mysql_impl_.swap(other.mysql_impl_);
  rpl_impl_.swap(other.rpl_impl_);
}

std::uint32_t connection::get_server_version() const noexcept {
  assert(!is_empty());
  return util::maybe_useless_integral_cast<std::uint32_t>(
      mysql_get_server_version(mysql_deimpl::get_const_casted(mysql_impl_)));
}

std::string_view connection::get_readable_server_version() const noexcept {
  assert(!is_empty());
  return {mysql_get_server_info(mysql_deimpl::get_const_casted(mysql_impl_))};
}

std::uint32_t connection::get_protocol_version() const noexcept {
  assert(!is_empty());
  return util::maybe_useless_integral_cast<std::uint32_t>(
      mysql_get_proto_info(mysql_deimpl::get_const_casted(mysql_impl_)));
}

std::string_view connection::get_server_connection_info() const noexcept {
  assert(!is_empty());
  return {mysql_get_host_info(mysql_deimpl::get_const_casted(mysql_impl_))};
}

std::string_view connection::get_character_set_name() const noexcept {
  assert(!is_empty());
  return {
      mysql_character_set_name(mysql_deimpl::get_const_casted(mysql_impl_))};
}

void connection::execute_generic_query_noresult(std::string_view query) {
  assert(!is_empty());
  if (is_in_replication_mode()) {
    util::exception_location().raise<std::logic_error>(
        "cannot execute query in replication mode");
  }

  auto *casted_impl = mysql_deimpl::get(mysql_impl_);
  if (mysql_real_query(casted_impl, std::data(query), std::size(query)) != 0) {
    raise_core_error_from_connection("cannot execute query", *this);
  }
}

bool connection::ping() {
  assert(!is_empty());
  if (is_in_replication_mode()) {
    util::exception_location().raise<std::logic_error>(
        "cannot perform ping in replication mode");
  }

  auto *casted_impl = mysql_deimpl::get(mysql_impl_);
  return mysql_ping(casted_impl) == 0;
}

void connection::switch_to_replication(
    std::uint32_t server_id, std::string_view file_name, std::uint64_t position,
    connection_replication_mode_type blocking_mode) {
  assert(!is_empty());
  if (is_in_replication_mode()) {
    util::exception_location().raise<std::logic_error>(
        "connection has already been swithed to replication");
  }

  // WL#2540: Replication event checksums
  // https://dev.mysql.com/worklog/task/?id=2540
  static constexpr std::string_view crc_query{
      "SET @source_binlog_checksum = 'NONE', "
      "@master_binlog_checksum = 'NONE'"};
  execute_generic_query_noresult(crc_query);

  rpl_impl_ = std::make_unique<rpl_impl>(*this, server_id, file_name, position,
                                         blocking_mode);
}

bool connection::fetch_binlog_event(util::const_byte_span &portion) {
  assert(!is_empty());
  if (!is_in_replication_mode()) {
    util::exception_location().raise<std::logic_error>(
        "connection has not been switched to replication");
  }

  const auto impl_fetch_result{rpl_impl_->fetch(portion)};

  if (!impl_fetch_result) {
    const auto native_error = mysql_errno(mysql_deimpl::get(mysql_impl_));
    if (native_error != CR_SERVER_LOST) {
      raise_core_error_from_connection("cannot fetch binlog event", *this);
    }
  }

  return impl_fetch_result;
}

} // namespace easymysql
