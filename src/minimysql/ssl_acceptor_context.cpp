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

#include "minimysql/ssl_acceptor_context.hpp"

#include <array>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/verify_mode.hpp>

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace minimysql {

namespace {

// Drain the OpenSSL error queue into a single human-readable string. Used to
// annotate exceptions with the actual TLS failure reason (unknown file,
// invalid PEM, etc.) instead of the generic Boost.Asio message.
//
// Uses ERR_error_string_n() with a local stack buffer rather than
// ERR_error_string(code, nullptr): the latter writes to a shared static
// buffer that is not thread-safe, so concurrent SSL failures on multiple
// io_context threads (a common scaling pattern we may adopt later) could
// clobber each other's messages.
std::string drain_openssl_error_queue() {
  // OpenSSL documents 256 bytes as sufficient for any error string.
  constexpr std::size_t error_string_buffer_size{256UZ};
  std::array<char, error_string_buffer_size> error_string_buffer{};

  std::string result;
  unsigned long error_code{0U};
  while ((error_code = ERR_get_error()) != 0U) {
    if (!result.empty()) {
      result.append("; ");
    }
    ERR_error_string_n(error_code, std::data(error_string_buffer),
                       std::size(error_string_buffer));
    result.append(std::data(error_string_buffer));
  }
  return result;
}

} // namespace

ssl_acceptor_context::ssl_acceptor_context(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view certificate_path, std::string_view private_key_path)
    : certificate_path_{certificate_path}, private_key_path_{private_key_path},
      // Version-agnostic TLS method (SSLv23_server_method in OpenSSL terms) so
      // both TLSv1.2 and TLSv1.3 clients can negotiate. tlsv12_server would
      // pin the server to TLSv1.2 and reject TLSv1.3 handshakes.
      context_{boost::asio::ssl::context::tls_server} {
  // Mirror Percona Server 8.0's ssl_ctx_options: keep only TLSv1.2 and
  // TLSv1.3 on the wire.
  context_.set_options(boost::asio::ssl::context::no_sslv2 |
                       boost::asio::ssl::context::no_sslv3 |
                       boost::asio::ssl::context::no_tlsv1 |
                       boost::asio::ssl::context::no_tlsv1_1);

  context_.set_verify_mode(boost::asio::ssl::verify_none);

  // Prefer Boost.Asio's SSL context wrappers over direct OpenSSL calls where
  // an equivalent exists. Use the error_code overloads so we can wrap the
  // resulting message with the offending path — richer than the generic
  // "certificate load failure" text a throwing overload would produce.
  //
  // Note: Boost's wrappers pop the top OpenSSL error into the error_code
  // themselves, so drain_openssl_error_queue() would return an empty string
  // here. Rely on error_code::message() for the underlying reason (bad PEM,
  // no such file, key/cert mismatch, …) — for the boost::asio SSL error
  // category, message() stringifies the OpenSSL reason.
  // clang-tidy's misc-include-cleaner wants a private
  // boost/system/detail/error_code.hpp include for error_code, which Boost
  // convention forbids; the type comes transitively via the ssl/context
  // header included above.
  // NOLINTNEXTLINE(misc-include-cleaner)
  boost::system::error_code error_code;

  context_.use_certificate_chain_file(certificate_path_, error_code);
  if (error_code) {
    throw std::runtime_error{"failed to load SSL certificate chain from '" +
                             certificate_path_ + "': " + error_code.message()};
  }

  context_.use_private_key_file(private_key_path_,
                                boost::asio::ssl::context::pem, error_code);
  if (error_code) {
    throw std::runtime_error{"failed to load SSL private key from '" +
                             private_key_path_ + "': " + error_code.message()};
  }

  // Boost.Asio's ssl::context has no wrapper for SSL_CTX_check_private_key,
  // so we call it directly on the underlying handle. Clear the OpenSSL error
  // queue first: a successful use_certificate_chain_file / use_private_key_file
  // above may have left residual entries (advisory warnings, etc.) that would
  // otherwise be prepended to the mismatch reason we drain below.
  ERR_clear_error();
  if (SSL_CTX_check_private_key(context_.native_handle()) != 1) {
    throw std::runtime_error{"SSL private key '" + private_key_path_ +
                             "' does not match certificate '" +
                             certificate_path_ +
                             "': " + drain_openssl_error_queue()};
  }
}

} // namespace minimysql
