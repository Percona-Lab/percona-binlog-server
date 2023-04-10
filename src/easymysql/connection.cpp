#include "easymysql/connection.hpp"

#include <cassert>
#include <stdexcept>

#include <mysql/mysql.h>

#include "easymysql/binlog.hpp"
#include "easymysql/connection_config.hpp"
#include "easymysql/core_error.hpp"

#include "util/conversion_helpers.hpp"
#include "util/exception_helpers.hpp"

namespace easymysql {

void connection::connection_deleter::operator()(void *ptr) const noexcept {
  if (ptr != nullptr) {
    mysql_close(static_cast<MYSQL *>(ptr));
  }
}

connection::connection(const connection_config &config)
    : impl_{mysql_init(nullptr)} {
  if (!impl_) {
    util::exception_location().raise<std::logic_error>(
        "cannot create MYSQL object");
  }
  auto *casted_impl = static_cast<MYSQL *>(impl_.get());
  if (mysql_real_connect(casted_impl,
                         /*        host */ config.get_host().c_str(),
                         /*        user */ config.get_user().c_str(),
                         /*    password */ config.get_password().c_str(),
                         /*          db */ nullptr,
                         /*        port */ config.get_port(),
                         /* unix socket */ nullptr,
                         /*       flags */ 0) == nullptr) {
    util::exception_location().raise<core_error>(
        static_cast<int>(mysql_errno(casted_impl)), mysql_error(casted_impl));
  }
}

std::uint32_t connection::get_server_version() const noexcept {
  assert(!is_empty());
  return util::maybe_useless_integral_cast<std::uint32_t>(
      mysql_get_server_version(static_cast<MYSQL *>(impl_.get())));
}

std::string_view connection::get_readable_server_version() const noexcept {
  assert(!is_empty());
  return {mysql_get_server_info(static_cast<MYSQL *>(impl_.get()))};
}

std::uint32_t connection::get_protocol_version() const noexcept {
  assert(!is_empty());
  return util::maybe_useless_integral_cast<std::uint32_t>(
      mysql_get_proto_info(static_cast<MYSQL *>(impl_.get())));
}

std::string_view connection::get_server_connection_info() const noexcept {
  assert(!is_empty());
  return {mysql_get_host_info(static_cast<MYSQL *>(impl_.get()))};
}

std::string_view connection::get_character_set_name() const noexcept {
  assert(!is_empty());
  return {mysql_character_set_name(static_cast<MYSQL *>(impl_.get()))};
}

binlog connection::create_binlog(std::uint32_t server_id) {
  return {*this, server_id};
}

void connection::execute_generic_query_noresult(std::string_view query) {
  assert(!is_empty());
  auto *casted_impl = static_cast<MYSQL *>(impl_.get());
  if (mysql_real_query(casted_impl, query.data(), query.size()) != 0) {
    util::exception_location().raise<core_error>(
        static_cast<int>(mysql_errno(casted_impl)), mysql_error(casted_impl));
  }
}

} // namespace easymysql
