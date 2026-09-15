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

#ifndef MINIMYSQL_SSL_ACCEPTOR_CONTEXT_HPP
#define MINIMYSQL_SSL_ACCEPTOR_CONTEXT_HPP

#include "minimysql/ssl_acceptor_context_fwd.hpp" // IWYU pragma: export

#include <string>
#include <string_view>

#include <boost/asio/ssl/context.hpp>

namespace minimysql {

// RAII wrapper around a single boost::asio::ssl::context configured as a
// MySQL-compatible TLS server acceptor. Owns the cert/key material for the
// lifetime of the listener and is shared (by reference) across all sessions.
//
// Configuration matches Percona Server 8.0 defaults:
//   - Base method: TLSv1.2 server (SSLv2/v3/TLSv1.0/v1.1 explicitly disabled),
//   - Certificate chain loaded from PEM,
//   - Private key loaded from PEM and matched against the cert
//     (SSL_CTX_check_private_key),
//   - Peer verification: SSL_VERIFY_NONE (server does not request a client
//     certificate).
class ssl_acceptor_context {
public:
  ssl_acceptor_context(std::string_view certificate_path,
                       std::string_view private_key_path);

  ssl_acceptor_context(const ssl_acceptor_context &) = delete;
  ssl_acceptor_context &operator=(const ssl_acceptor_context &) = delete;
  ssl_acceptor_context(ssl_acceptor_context &&) = delete;
  ssl_acceptor_context &operator=(ssl_acceptor_context &&) = delete;

  ~ssl_acceptor_context() = default;

  [[nodiscard]] boost::asio::ssl::context &native() noexcept {
    return context_;
  }

  [[nodiscard]] const std::string &get_certificate_path() const noexcept {
    return certificate_path_;
  }

  [[nodiscard]] const std::string &get_private_key_path() const noexcept {
    return private_key_path_;
  }

private:
  std::string certificate_path_;
  std::string private_key_path_;
  boost::asio::ssl::context context_;
};

} // namespace minimysql

#endif // MINIMYSQL_SSL_ACCEPTOR_CONTEXT_HPP
