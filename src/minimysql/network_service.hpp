// Copyright (c) 2023-2026 Percona and/or its affiliates.
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

#ifndef MINIMYSQL_NETWORK_SERVICE_HPP
#define MINIMYSQL_NETWORK_SERVICE_HPP

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>

#include <boost/asio/ts/netfwd.hpp>

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/replication_source_config_fwd.hpp"
#include "binsrv/storage_fwd.hpp"

#include "minimysql/connection_context_fwd.hpp"
#include "minimysql/network_io_operations_fwd.hpp"

#include "opensslpp/evp_pkey.hpp"

namespace minimysql {

class network_service {
public:
  static constexpr auto expected_packet_size{4096UZ};

  network_service(binsrv::basic_logger_ptr logger,
                  boost::asio::io_context &io_ctx, binsrv::storage_ptr storage,
                  const binsrv::replication_source_config &cfg);

  network_service(const network_service &) = delete;
  network_service &operator=(const network_service &) = delete;
  network_service(network_service &&) = delete;
  network_service &operator=(network_service &&) = delete;

  ~network_service();

private:
  binsrv::basic_logger_ptr logger_;
  boost::asio::io_context *io_ctx_;
  binsrv::storage_ptr storage_;
  const binsrv::replication_source_config *config_;

  struct impl_helper;
  struct ssl_context_deleter {
    void operator()(void *ssl_context) const noexcept;
  };
  using ssl_context_ptr = std::unique_ptr<void, ssl_context_deleter>;
  ssl_context_ptr ssl_context_;

  // empty if <replication_source.auth_rsa_private_key> is not configured
  opensslpp::evp_pkey auth_rsa_private_key_;
  // public component of 'auth_rsa_private_key_' in PEM format sent to the
  // clients that request it during full authentication (empty if no key)
  std::string auth_rsa_public_key_pem_;

  // caching_sha2_password authentication cache - as only one user account
  // is currently supported, a single flag is enough: it is set once this
  // user has successfully passed full authentication (subsequent connections
  // from this user go through fast authentication)
  std::atomic<bool> auth_cached_;

  using acceptor_type =
      boost::asio::basic_socket_acceptor<boost::asio::ip::tcp>;
  using acceptor_ptr = std::unique_ptr<acceptor_type>;
  acceptor_ptr acceptor_;

  [[nodiscard]] boost::asio::io_context &get_io_ctx() const noexcept {
    return *io_ctx_;
  }
  [[nodiscard]] const binsrv::replication_source_config &
  get_config() const noexcept {
    return *config_;
  }

  [[nodiscard]] bool is_auth_rsa_key_available() const noexcept {
    return !auth_rsa_private_key_.is_empty();
  }
  [[nodiscard]] bool is_ssl_tls_available() const noexcept {
    return static_cast<bool>(ssl_context_);
  }

  void print_server_greeting(std::string_view remote_endpoint,
                             const connection_context &context);
  void print_client_greeting(std::string_view remote_endpoint,
                             const connection_context &context);
  void print_client_auth_method_switch(std::string_view remote_endpoint,
                                       const connection_context &context);
  void print_generic(std::string_view remote_endpoint,
                     const connection_context &context, std::string_view label);
  void print_error(std::string_view remote_endpoint,
                   const connection_context &context, std::string_view label);
  void print_client_command(std::string_view remote_endpoint,
                            const connection_context &context);
  void handle_exception(std::string_view context);

  [[nodiscard]] bool
  check_if_in_auth_cache(const connection_context &context) const noexcept;
  void add_to_auth_cache(const connection_context &context);

  // for the 'socket' we use only the types available in the 'netfwd.hpp'
  [[nodiscard]] boost::asio::awaitable<void> handle_binlog_dump_command(
      boost::asio::basic_stream_socket<boost::asio::ip::tcp> &socket,
      connection_context &context, bool session_source_binlog_checksum,
      std::chrono::nanoseconds session_heartbeat_period);

  [[nodiscard]] boost::asio::awaitable<bool> perform_fast_authentication(
      boost::asio::basic_stream_socket<boost::asio::ip::tcp> &socket,
      connection_context &context);
  [[nodiscard]] boost::asio::awaitable<void> perform_public_key_dispatch(
      boost::asio::basic_stream_socket<boost::asio::ip::tcp> &socket,
      connection_context &context);
  [[nodiscard]] boost::asio::awaitable<bool> perform_full_authentication(
      boost::asio::basic_stream_socket<boost::asio::ip::tcp> &socket,
      connection_context &context);
  [[nodiscard]] boost::asio::awaitable<bool> perform_connection_packet_exchange(
      boost::asio::basic_stream_socket<boost::asio::ip::tcp> &socket,
      connection_context &context);

  [[nodiscard]] boost::asio::awaitable<void>
  session(boost::asio::basic_stream_socket<boost::asio::ip::tcp> socket);
  [[nodiscard]] boost::asio::awaitable<void> listener();
  void load_optional_auth_rsa_private_key();
  void initialize_optional_ssl_context();

  [[nodiscard]] static std::string extract_remote_endpoint_str(
      const boost::asio::basic_stream_socket<boost::asio::ip::tcp> &socket);
};

} // namespace minimysql

#endif // MINIMYSQL_NETWORK_SERVICE_HPP
