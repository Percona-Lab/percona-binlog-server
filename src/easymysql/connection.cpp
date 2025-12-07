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
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <mysql/errmsg.h>
#include <mysql/mysql.h>

#include "easymysql/connection_config.hpp"
#include "easymysql/connection_deimpl_private.hpp"
#include "easymysql/core_error_helpers_private.hpp"
#include "easymysql/ssl_mode_type.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/conversion_helpers.hpp"
#include "util/ct_string.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/nv_tuple_fwd.hpp"

namespace {

// enum mysql_ssl_mode (SSL_MODE_XXX) constants
// https://github.com/mysql/mysql-server/blob/mysql-8.0.43/include/mysql.h#L271
// https://github.com/mysql/mysql-server/blob/mysql-8.4.6/include/mysql.h#L272
auto convert_ssl_mode_to_native(easymysql::ssl_mode_type ssl_mode) noexcept {
  switch (ssl_mode) {
  case easymysql::ssl_mode_type::disabled:
    return SSL_MODE_DISABLED;
  case easymysql::ssl_mode_type::preferred:
    return SSL_MODE_PREFERRED;
  case easymysql::ssl_mode_type::required:
    return SSL_MODE_REQUIRED;
  case easymysql::ssl_mode_type::verify_ca:
    return SSL_MODE_VERIFY_CA;
  case easymysql::ssl_mode_type::verify_identity:
    return SSL_MODE_VERIFY_IDENTITY;

  default:
    assert(false);
  }
  // this return should never happen
  return mysql_ssl_mode{};
}

[[nodiscard]] bool
set_generic_mysql_option_helper(MYSQL *impl, mysql_option option_id,
                                const std::string &value) noexcept {
  return mysql_options(impl, option_id, value.c_str()) == 0;
}
template <std::unsigned_integral T>
[[nodiscard]] bool set_generic_mysql_option_helper(MYSQL *impl,
                                                   mysql_option option_id,
                                                   T value) noexcept {
  const unsigned int casted_value{value};
  return mysql_options(impl, option_id, &casted_value) == 0;
}
[[nodiscard]] bool
set_generic_mysql_option_helper(MYSQL *impl, mysql_option option_id,
                                easymysql::ssl_mode_type value) noexcept {
  const unsigned int converted_value{convert_ssl_mode_to_native(value)};
  return set_generic_mysql_option_helper(impl, option_id, converted_value);
}

template <typename T>
[[nodiscard]] bool
set_generic_mysql_option_helper(MYSQL *impl, mysql_option option_id,
                                const std::optional<T> &value) noexcept {
  if (!value.has_value()) {
    return true;
  }
  return set_generic_mysql_option_helper(impl, option_id, value.value());
}

template <util::ct_string CTS, util::derived_from_named_value_tuple Config>
[[nodiscard]] bool set_generic_mysql_option(const Config &config, MYSQL *impl,
                                            mysql_option option_id) noexcept {
  const auto &value{config.template get<CTS>()};
  return set_generic_mysql_option_helper(impl, option_id, value);
}

} // anonymous namespace

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
  // https://github.com/mysql/mysql-server/blob/mysql-cluster-8.0.43/include/mysql.h#L366
  // https://github.com/mysql/mysql-server/blob/mysql-cluster-8.4.6/include/mysql.h#L367

  // USE_HEARTBEAT_EVENT_V2
  // https://github.com/mysql/mysql-server/blob/mysql-cluster-8.0.43/include/mysql.h#L372
  // https://github.com/mysql/mysql-server/blob/mysql-cluster-8.4.6/include/mysql.h#L373

  // Explaining BINLOG_DUMP_NON_BLOCK
  // https://github.com/mysql/mysql-server/blob/mysql-8.0.43/sql/rpl_constants.h#L45
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/sql/rpl_constants.h#L45
  // https://github.com/mysql/mysql-server/blob/mysql-8.0.43/sql/rpl_binlog_sender.cc#L313
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/sql/rpl_binlog_sender.cc#L320

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

void connection::process_ssl_config(const ssl_config &config) {
  auto *casted_impl = mysql_deimpl::get(mysql_impl_);

  // MYSQL_OPT_SSL_MODE
  if (!set_generic_mysql_option<"mode">(config, casted_impl,
                                        MYSQL_OPT_SSL_MODE)) {
    raise_core_error_from_connection("cannot set MySQL SSL mode", *this);
  }

  // MYSQL_OPT_SSL_CA
  if (!set_generic_mysql_option<"ca">(config, casted_impl, MYSQL_OPT_SSL_CA)) {
    raise_core_error_from_connection("cannot set MySQL SSL CA", *this);
  }

  // MYSQL_OPT_SSL_CAPATH
  if (!set_generic_mysql_option<"capath">(config, casted_impl,
                                          MYSQL_OPT_SSL_CAPATH)) {
    raise_core_error_from_connection("cannot set MySQL SSL CAPATH", *this);
  }

  // MYSQL_OPT_SSL_CRL
  if (!set_generic_mysql_option<"crl">(config, casted_impl,
                                       MYSQL_OPT_SSL_CRL)) {
    raise_core_error_from_connection("cannot set MySQL SSL CRL", *this);
  }

  // MYSQL_OPT_SSL_CRLPATH
  if (!set_generic_mysql_option<"crlpath">(config, casted_impl,
                                           MYSQL_OPT_SSL_CRLPATH)) {
    raise_core_error_from_connection("cannot set MySQL SSL CRLPATH", *this);
  }

  // MYSQL_OPT_SSL_CERT
  if (!set_generic_mysql_option<"cert">(config, casted_impl,
                                        MYSQL_OPT_SSL_CERT)) {
    raise_core_error_from_connection("cannot set MySQL SSL CERT", *this);
  }

  // MYSQL_OPT_SSL_KEY
  if (!set_generic_mysql_option<"key">(config, casted_impl,
                                       MYSQL_OPT_SSL_KEY)) {
    raise_core_error_from_connection("cannot set MySQL SSL KEY", *this);
  }

  // MYSQL_OPT_SSL_CIPHER
  if (!set_generic_mysql_option<"cipher">(config, casted_impl,
                                          MYSQL_OPT_SSL_CIPHER)) {
    raise_core_error_from_connection("cannot set MySQL SSL CIPHER", *this);
  }
}

void connection::process_tls_config(const tls_config &config) {
  auto *casted_impl = mysql_deimpl::get(mysql_impl_);

  // MYSQL_OPT_TLS_CIPHERSUITES
  if (!set_generic_mysql_option<"ciphersuites">(config, casted_impl,
                                                MYSQL_OPT_TLS_CIPHERSUITES)) {
    raise_core_error_from_connection("cannot set MySQL TLS CIPHERSUITES",
                                     *this);
  }

  // MYSQL_OPT_TLS_VERSION
  if (!set_generic_mysql_option<"version">(config, casted_impl,
                                           MYSQL_OPT_TLS_VERSION)) {
    raise_core_error_from_connection("cannot set MySQL TLS VERSION", *this);
  }
}

void connection::process_connection_config(const connection_config &config) {
  auto *casted_impl = mysql_deimpl::get(mysql_impl_);

  // MYSQL_OPT_CONNECT_TIMEOUT
  if (!set_generic_mysql_option<"connect_timeout">(config, casted_impl,
                                                   MYSQL_OPT_CONNECT_TIMEOUT)) {
    raise_core_error_from_connection("cannot set MySQL connect timeout", *this);
  }
  // MYSQL_OPT_READ_TIMEOUT
  if (!set_generic_mysql_option<"read_timeout">(config, casted_impl,
                                                MYSQL_OPT_READ_TIMEOUT)) {
    raise_core_error_from_connection("cannot set MySQL read timeout", *this);
  }
  // MYSQL_OPT_WRITE_TIMEOUT
  if (!set_generic_mysql_option<"write_timeout">(config, casted_impl,
                                                 MYSQL_OPT_WRITE_TIMEOUT)) {
    raise_core_error_from_connection("cannot set MySQL write timeout", *this);
  }

  const auto &opt_ssl_config{config.get<"ssl">()};
  if (opt_ssl_config.has_value()) {
    process_ssl_config(opt_ssl_config.value());
  }

  const auto &opt_tls_config{config.get<"tls">()};
  if (opt_tls_config.has_value()) {
    process_tls_config(opt_tls_config.value());
  }
}

connection::connection(const connection_config &config)
    : mysql_impl_{mysql_init(nullptr)}, rpl_impl_{} {
  if (!mysql_impl_) {
    util::exception_location().raise<std::logic_error>(
        "cannot create MYSQL object");
  }
  auto *casted_impl = mysql_deimpl::get(mysql_impl_);

  process_connection_config(config);

  const std::string empty_string{};
  if (config.has_dns_srv_name()) {
    const auto &opt_dns_srv_name{config.get<"dns_srv_name">()};
    const auto &dns_srv_name{
        opt_dns_srv_name.has_value() ? opt_dns_srv_name.value() : empty_string};
    if (mysql_real_connect_dns_srv(
            casted_impl,
            /* dns_srv_name */ dns_srv_name.c_str(),
            /*         user */ config.get<"user">().c_str(),
            /*       passwd */ config.get<"password">().c_str(),
            /*           db */ nullptr,
            /*  client_flag */ 0) == nullptr) {
      raise_core_error_from_connection("cannot establish MySQL connection",
                                       *this);
    }
  } else {
    const auto &opt_host{config.get<"host">()};
    const auto &host{opt_host.has_value() ? opt_host.value() : empty_string};
    const auto port{config.get<"port">().value_or(0U)};
    if (mysql_real_connect(casted_impl,
                           /*        host */ host.c_str(),
                           /*        user */ config.get<"user">().c_str(),
                           /*      passwd */ config.get<"password">().c_str(),
                           /*          db */ nullptr,
                           /*        port */ port,
                           /* unix_socket */ nullptr,
                           /* client_flag */ 0) == nullptr) {
      raise_core_error_from_connection("cannot establish MySQL connection",
                                       *this);
    }
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
// destroyed in the order reverse to how they were declared, e.g.
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
    bool verify_checksum, bool gtid_mode,
    connection_replication_mode_type blocking_mode) {
  assert(!is_empty());
  if (gtid_mode) {
    util::exception_location().raise<std::logic_error>(
        "switching to GTID replication is not yet implemented");
  }
  if (is_in_replication_mode()) {
    util::exception_location().raise<std::logic_error>(
        "connection has already been swithed to replication");
  }

  // WL#2540: Replication event checksums
  // https://dev.mysql.com/worklog/task/?id=2540
  const std::string checksum_algorithm_label{verify_checksum ? "CRC32"
                                                             : "NONE"};
  const auto set_binlog_checksum_query{
      "SET @source_binlog_checksum = '" + checksum_algorithm_label +
      "', @master_binlog_checksum = '" + checksum_algorithm_label + "'"};
  execute_generic_query_noresult(set_binlog_checksum_query);

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
