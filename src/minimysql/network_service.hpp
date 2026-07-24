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

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/ts/netfwd.hpp>

#include "minimysql/ssl_acceptor_context_fwd.hpp"

namespace minimysql {

class network_service {
public:
  static constexpr auto expected_packet_size{4096UZ};
  static constexpr std::chrono::seconds session_authentication_timeout{10};
  static constexpr std::chrono::seconds session_command_timeout{120};

  // `ssl_ctx` is an optional owning handle. When non-empty, the server
  // advertises CLIENT_SSL in its greeting and upgrades the transport to TLS
  // on receipt of a Protocol::SSLRequest. When empty, the listener behaves
  // exactly like the plaintext-only version (no SSL capability advertised,
  // no upgrade path). Construction of the ssl_acceptor_context must happen
  // in the caller — a failure there (bad cert/key path, mismatched pair)
  // surfaces before network_service is instantiated instead of throwing
  // from this constructor. Ownership is transferred by move; the caller
  // does not retain a handle.
  //
  // No default argument for `ssl_ctx` because libc++'s `unique_ptr` requires
  // the complete type at the point where the default-argument destructor is
  // instantiated. Callers wanting the plaintext-only listener pass
  // `nullptr` (or an empty unique_ptr) explicitly.
  network_service(boost::asio::io_context &context,
                  std::uint16_t listening_port, std::string_view username,
                  std::string_view password,
                  std::string_view server_rsa_public_key_path,
                  std::string_view server_rsa_private_key_path,
                  std::unique_ptr<ssl_acceptor_context> ssl_ctx);

  network_service(const network_service &) = delete;
  network_service &operator=(const network_service &) = delete;
  network_service(network_service &&) = delete;
  network_service &operator=(network_service &&) = delete;

  ~network_service();

private:
  std::string username_;
  std::string password_;
  std::string server_rsa_public_key_path_;
  std::string server_rsa_private_key_path_;

  boost::asio::io_context *context_;
  // Owned SSL acceptor state. Empty when the listener is plaintext-only.
  std::unique_ptr<ssl_acceptor_context> ssl_ctx_;
  using acceptor_type =
      boost::asio::basic_socket_acceptor<boost::asio::ip::tcp>;
  using acceptor_ptr = std::unique_ptr<acceptor_type>;
  acceptor_ptr acceptor_;
};

} // namespace minimysql

#endif // MINIMYSQL_NETWORK_SERVICE_HPP
