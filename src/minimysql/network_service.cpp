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

#include "minimysql/network_service.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/lexical_cast.hpp>

#include <boost/asio/awaitable.hpp>
// Include What You Use pragma is needed here because the 'co_spawn()'
// function that is used in this file is located in the 'impl' subdirectory
// of the 'asio' headers ('boost/asio/impl/co_spawn.hpp') and should not be
// included directly, but the 'boost/asio/co_spawn.hpp' header is a public
// one that includes the 'impl' header
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp> // IWYU pragma: keep
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#pragma GCC diagnostic pop

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>

#include <boost/describe/enum_to_string.hpp>

#include <boost/system/system_error.hpp>

#include "minimysql/caching_sha2_password_authenticator.hpp"
#include "minimysql/connection_context.hpp"
#include "minimysql/network_io_operations.hpp"
#include "minimysql/sample_event_collection.hpp"
#include "minimysql/ssl_acceptor_context.hpp"

namespace minimysql {

namespace {

class scope_tracer {
public:
  explicit scope_tracer(std::string_view name) : name_(name) {
    std::cout << "entering " << name_ << '\n';
  }
  scope_tracer(const scope_tracer &) = delete;
  scope_tracer &operator=(const scope_tracer &) = delete;
  scope_tracer(scope_tracer &&) = delete;
  scope_tracer &operator=(scope_tracer &&) = delete;

  ~scope_tracer() { std::cout << "leaving " << name_ << '\n'; }

private:
  std::string name_;
};

void print_server_greeting(
    const boost::asio::ip::tcp::endpoint &remote_endpoint,
    const minimysql::connection_context &context) {
  std::cout
      << "generated server greeting for " << remote_endpoint << '\n'
      << "[sequence_number "
      << static_cast<std::uint16_t>(context.get_sequence_number() - 1U) << "]\n"
      << "  protocol_version: "
      << static_cast<std::uint16_t>(
             minimysql::connection_context::default_server_protocol_version)
      << '\n'
      << "  server_version  : "
      << minimysql::connection_context::default_server_version << '\n'
      << "  connection_id   : " << context.get_connection_id() << '\n'
      << "  collation       : "
      << static_cast<std::uint16_t>(
             minimysql::connection_context::default_server_collation)
      << '\n'
      << "  status flags    : "
      << minimysql::connection_context::default_server_status_flags << '\n'
      << "  auth_method     : " << context.get_server_auth_method() << '\n'
      << "  auth_method_data: "
      << std::size(context.get_server_auth_method_data()) << " byte(s)\n";
}

void print_client_greeting(
    const boost::asio::ip::tcp::endpoint &remote_endpoint,
    const minimysql::connection_context &context) {
  std::cout << "parsed client greeting from " << remote_endpoint << '\n'
            << "[sequence_number "
            << static_cast<std::uint16_t>(context.get_sequence_number() - 1U)
            << "]\n"
            << "  auth_method     : " << context.get_client_auth_method()
            << '\n'
            << "  auth_method_data: "
            << std::size(context.get_client_auth_method_data()) << " byte(s)\n"
            << "  username        : " << context.get_client_username() << '\n'
            << "  schema          : " << context.get_client_schema() << '\n'
            << "  collation       : "
            << static_cast<std::uint16_t>(context.get_client_collation())
            << '\n'
            << "  max_packet_size : " << context.get_client_max_packet_size()
            << '\n';
}
void print_generic(const boost::asio::ip::tcp::endpoint &remote_endpoint,
                   const minimysql::connection_context &context,
                   std::string_view label) {
  std::cout << "generated " << label << " packet for " << remote_endpoint
            << '\n'
            << "[sequence_number "
            << static_cast<std::uint16_t>(context.get_sequence_number() - 1U)
            << "]\n";
}
void print_error(const boost::asio::ip::tcp::endpoint &remote_endpoint,
                 const minimysql::connection_context &context,
                 std::string_view label) {
  std::cout << "generated error packet (" << label << ") for "
            << remote_endpoint << '\n'
            << "[sequence_number "
            << static_cast<std::uint16_t>(context.get_sequence_number() - 1U)
            << "]\n";
}
void print_client_command(const boost::asio::ip::tcp::endpoint &remote_endpoint,
                          const minimysql::connection_context &context) {
  std::cout << "parsed client command from " << remote_endpoint << '\n'
            << "[sequence_number "
            << static_cast<std::uint16_t>(context.get_sequence_number() - 1U)
            << "]\n"
            << "  code            : "
            << boost::describe::enum_to_string(
                   context.get_client_mysql_command(), "unknown")
            << '\n';
  if (context.get_client_mysql_command() ==
      minimysql::client_command_type::query) {
    std::cout << "  statement       : " << context.get_client_statement()
              << '\n';
  }
  if (context.get_client_mysql_command() ==
      minimysql::client_command_type::binlog_dump) {
    std::cout << "  binlog blocking mode : "
              << (context.check_binlog_non_blocking_dump() ? "non-" : "")
              << "blocking\n";
    std::cout << "  binlog server id     : " << context.get_binlog_server_id()
              << '\n';
    std::cout << "  binlog file name     : " << context.get_binlog_filename()
              << '\n';
    std::cout << "  binlog position      : " << context.get_binlog_position()
              << '\n';
  }
}

// a helper function to log exceptions with context information
void handle_exception(std::string_view context) {
  try {
    throw;
  } catch (const boost::system::system_error &e) {
    bool logged_specific_error{false};
    const auto exception_error_code{e.code()};
    if (exception_error_code.category() ==
        boost::asio::error::get_system_category()) {
      switch (exception_error_code.value()) {
      case boost::asio::error::timed_out:
        std::cout << context << ": " << e.what() << '\n';
        logged_specific_error = true;
        break;
      case boost::asio::error::operation_aborted:
        std::cout << context << ": " << "operation aborted" << '\n';
        logged_specific_error = true;
        break;
      case boost::asio::error::eof:
      case boost::asio::error::connection_reset:
        std::cout << context << ": " << "connection closed by peer" << '\n';
        logged_specific_error = true;
        break;
      default:
        // for other system errors we log the error code and message and rethrow
        break;
      }
    }

    if (!logged_specific_error) {
      std::cerr << "system error caught in " << context << ": " << e.code()
                << '\n'
                << e.what() << '\n';
    }
  } catch (const std::exception &e) {
    std::cerr << "exception caught in " << context << ": " << e.what() << '\n';
  } catch (...) {
    std::cerr << "unknown exception caught in " << context << '\n';
  }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"

// Runs the post-greeting authentication exchange on `socket`. Returns true
// iff the client authenticated successfully and the server "OK after auth"
// frame has been written. On any failure path (plugin auth unsupported, bad
// credentials) the caller has nothing more to do: this function has already
// written an "access denied" error frame and returned false, so the caller
// should tear the connection down.
template <typename Socket>
[[nodiscard]] boost::asio::awaitable<bool> perform_authentication(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    Socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    minimysql::connection_context &context,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const boost::asio::ip::tcp::endpoint &remote_endpoint,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    minimysql::network_buffer_type &data) {
  if (!context.check_shared_plugin_auth_supported()) {
    std::cout << "client does not support plugin authentication\n";
    const auto access_denied{context.generate_encoded_access_denied()};
    print_error(remote_endpoint, context, "plugin auth required");
    co_await minimysql::async_write_mysql_frame(
        socket, access_denied, network_service::session_authentication_timeout);
    std::cout << "sent server access denied (" << std::size(access_denied)
              << " bytes to " << remote_endpoint << ")\n";
    co_return false;
  }

  if (context.needs_auth_method_switch()) {
    const auto auth_method_switch{
        context.generate_encoded_auth_method_switch()};
    print_generic(remote_endpoint, context, "auth method switch");
    co_await minimysql::async_write_mysql_frame(
        socket, auth_method_switch,
        network_service::session_authentication_timeout);
    std::cout << "sent server auth method switch ("
              << std::size(auth_method_switch) << " bytes to "
              << remote_endpoint << ")\n";

    co_await minimysql::async_read_mysql_frame(
        socket, data, network_service::session_authentication_timeout);
    std::cout << "received client auth method switch response ("
              << std::size(data) << " bytes from " << remote_endpoint << ")\n";
    context.parse_client_auth_method_data(data);
    std::cout << "client auth method after switch: "
              << context.get_client_auth_method() << '\n'
              << "  auth_method_data: "
              << std::size(context.get_client_auth_method_data())
              << " byte(s)\n";
  }

  context.begin_authentication();

  for (;;) {
    // An authenticator may produce several outbound AuthMoreData frames
    // before it needs client input (for example fast-auth success plus a
    // follow-up, or a multi-step RSA exchange). The inner loop sends every
    // frame queued by begin_authentication() or submit_authentication_frame()
    // in order; only then does the outer loop read the next client packet.
    for (const auto &outbound_frame :
         context.take_authentication_outbound_frames()) {
      print_generic(remote_endpoint, context, "auth method data");
      co_await minimysql::async_write_mysql_frame(
          socket, outbound_frame,
          network_service::session_authentication_timeout);
      std::cout << "sent server authentication packet ("
                << std::size(outbound_frame) << " bytes to " << remote_endpoint
                << ")\n";
    }

    if (context.authentication_state() !=
        minimysql::authentication_state::in_progress) {
      break;
    }

    if (!context.expects_authentication_input()) {
      break;
    }

    co_await minimysql::async_read_mysql_frame(
        socket, data, network_service::session_authentication_timeout);
    std::cout << "received client authentication packet (" << std::size(data)
              << " bytes from " << remote_endpoint << ")\n";
    context.submit_authentication_frame(data);
  }

  if (context.authentication_state() !=
      minimysql::authentication_state::succeeded) {
    std::cout << "client authentication failed for "
              << context.get_client_username() << '\n';
    const auto access_denied{context.generate_encoded_access_denied()};
    print_error(remote_endpoint, context, "auth failure");
    co_await minimysql::async_write_mysql_frame(
        socket, access_denied, network_service::session_authentication_timeout);
    std::cout << "sent server access denied (" << std::size(access_denied)
              << " bytes to " << remote_endpoint << ")\n";
    co_return false;
  }

  std::cout << "client authentication succeeded for "
            << context.get_client_username() << '\n';

  // sending server ok after successful authentication
  const auto auth_ok{context.generate_encoded_ok()};
  print_generic(remote_endpoint, context, "ok (auth)");
  co_await minimysql::async_write_mysql_frame(
      socket, auth_ok, network_service::session_authentication_timeout);
  std::cout << "sent server ok after authentication (" << std::size(auth_ok)
            << " bytes to " << remote_endpoint << ")\n";

  co_return true;
}

// Post-greeting session body. Templated on the socket type so it runs on
// either a raw boost::asio::ip::tcp::socket (plaintext) or an
// ssl::stream<tcp::socket> (after a successful TLS upgrade). Once the
// server greeting and the client greeting (SSLRequest or full) have been
// exchanged on the ORIGINAL socket, control transfers here on the socket
// the rest of the session should use.
template <typename Socket>
boost::asio::awaitable<void> session_body(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    Socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    minimysql::connection_context &context,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const boost::asio::ip::tcp::endpoint &remote_endpoint,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    minimysql::network_buffer_type &data) {
  if (!co_await perform_authentication(socket, context, remote_endpoint,
                                       data)) {
    co_return;
  }

  // defining known queries container
  using query_handler_type = std::function<minimysql::network_buffer_container(
      minimysql::connection_context & context)>;
  using query_container = std::unordered_map<std::string, query_handler_type>;

  const auto set_checksum_query_handler =
      [](minimysql::connection_context &ctx) {
        minimysql::network_buffer_container resultset;
        resultset.emplace_back(ctx.generate_encoded_ok());
        return resultset;
      };
  query_container known_queries{
      {"select * from tbl",
       [](minimysql::connection_context &ctx) {
         using row_type =
             std::tuple<std::uint64_t, std::optional<std::uint64_t>,
                        std::string, std::optional<std::string>>;
         using row_collection_type = std::vector<row_type>;
         const row_collection_type rows{{1, 100, "Alice", "Cooper"},
                                        {2, {}, "Bob", {}}};
         const std::array column_names{
             minimysql::column_name_pair{"id", "id"},
             minimysql::column_name_pair{"optional_id", "optional_id"},
             minimysql::column_name_pair{"name", "name"},
             minimysql::column_name_pair{"optional_name", "optional_name"}};
         return ctx.encode_resultset(rows, column_names);
       }},
      {"select @@version_comment limit 1",
       [](minimysql::connection_context &ctx) {
         using version_comment_record = std::tuple<std::string>;
         using version_comment_record_collection =
             std::vector<version_comment_record>;
         const version_comment_record_collection records{
             {"Percona Binlog Server - GPL"}};
         const std::array column_names{
             minimysql::column_name_pair{"@@version_comment", ""}};
         return ctx.encode_resultset(records, column_names);
       }},
      {"SELECT VERSION()",
       [](minimysql::connection_context &ctx) {
         using version_record = std::tuple<std::string>;
         using version_record_collection = std::vector<version_record>;
         const version_record_collection records{{"9.7.0"}};
         const std::array column_names{
             minimysql::column_name_pair{"VERSION()", ""}};
         return ctx.encode_resultset(records, column_names);
       }},
      {"SET @source_binlog_checksum = 'NONE', @master_binlog_checksum = "
       "'NONE'",
       set_checksum_query_handler},
      {"SET @master_binlog_checksum = 'NONE', @source_binlog_checksum = "
       "'NONE'",
       set_checksum_query_handler}};

  // starting command loop
  bool terminated{false};
  while (!terminated) {
    context.enter_command_loop_iteration();
    co_await minimysql::async_read_mysql_frame(
        socket, data, network_service::session_command_timeout);
    std::cout << "received client command (" << std::size(data)
              << " bytes from " << remote_endpoint << ")\n";
    context.parse_client_command(data);
    print_client_command(remote_endpoint, context);

    switch (context.get_client_mysql_command()) {
    case minimysql::client_command_type::query: {
      const auto known_query_it{
          known_queries.find(context.get_client_statement())};
      if (known_query_it != std::end(known_queries)) {
        const auto resultset{known_query_it->second(context)};
        print_generic(remote_endpoint, context, "resultset");
        co_await minimysql::async_write_mysql_frames(
            socket, resultset, network_service::session_command_timeout);
        std::cout << "sent server resultset (" << std::size(resultset)
                  << " frames to " << remote_endpoint << ")\n";
      } else {
        // return 'syntax error' for every other query
        const auto syntax_error = context.generate_encoded_syntax_error();
        print_error(remote_endpoint, context, "syntax error");
        co_await minimysql::async_write_mysql_frame(
            socket, syntax_error, network_service::session_command_timeout);
        std::cout << "sent server syntax error (" << std::size(syntax_error)
                  << " bytes to " << remote_endpoint << ")\n";
      }
    } break;
    case minimysql::client_command_type::ping: {
      const auto ok_after_ping{context.generate_encoded_ok()};
      print_generic(remote_endpoint, context, "ok (ping success)");
      co_await minimysql::async_write_mysql_frame(
          socket, ok_after_ping, network_service::session_command_timeout);
      std::cout << "sent server ok after ping (" << std::size(ok_after_ping)
                << " bytes to " << remote_endpoint << ")\n";
    } break;
    case minimysql::client_command_type::binlog_dump: {
      const minimysql::sample_event_collection sample_events;
      for (const auto &event_data : sample_events.get_events()) {
        const auto event{context.generate_encoded_binlog_event(event_data)};
        print_generic(remote_endpoint, context, "binlog event");
        co_await minimysql::async_write_mysql_frame(
            socket, event, network_service::session_command_timeout);
        std::cout << "sent server binlog event (" << std::size(event)
                  << " bytes to " << remote_endpoint << ")\n";
      }
      const auto eof = context.generate_encoded_eof();
      print_generic(remote_endpoint, context, "binlog eof");
      co_await minimysql::async_write_mysql_frame(
          socket, eof, network_service::session_command_timeout);
      std::cout << "sent server eof (" << std::size(eof) << " bytes to "
                << remote_endpoint << ")\n";
      terminated = true;
    } break;
    case minimysql::client_command_type::quit: {
      // TODO: read EOF from the socket to make sure the client has closed the
      // connection instead of just closing it from our side
      terminated = true;
    } break;
    default: {
      const auto unknown_command_error =
          context.generate_encoded_unknown_command();
      print_error(remote_endpoint, context, "unknown command");
      co_await minimysql::async_write_mysql_frame(
          socket, unknown_command_error,
          network_service::session_command_timeout);
      std::cout << "sent server unknown command ("
                << std::size(unknown_command_error) << " bytes to "
                << remote_endpoint << ")\n";
    }
    }
  }
}

// Perform a boost::asio::ssl::stream::async_handshake as server, bounded by
// the same timeout used for the rest of the authentication phase. On timeout
// or handshake error, throws a boost::system::system_error which the outer
// session catch handler logs.
boost::asio::awaitable<void> perform_ssl_handshake(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> &ssl_socket,
    std::chrono::steady_clock::duration timeout) {
  using namespace boost::asio::experimental::awaitable_operators;

  boost::asio::steady_timer handshake_timer{ssl_socket.get_executor(), timeout};
  auto timed_handshake_result{
      co_await (ssl_socket.async_handshake(
                    boost::asio::ssl::stream_base::server,
                    boost::asio::as_tuple(boost::asio::use_awaitable)) ||
                handshake_timer.async_wait(
                    boost::asio::as_tuple(boost::asio::use_awaitable)))};

  if (timed_handshake_result.index() != 0UZ) {
    throw boost::system::system_error{boost::asio::error::timed_out,
                                      "TLS handshake timeout"};
  }

  const auto &handshake_result{std::get<0UZ>(timed_handshake_result)};
  const auto handshake_error_code{std::get<0UZ>(handshake_result)};
  if (handshake_error_code) {
    throw boost::system::system_error{handshake_error_code,
                                      "TLS handshake error"};
  }
}

// MySQL session handling coroutine - writes server greeting, then receives
// and parses client greeting. On a Protocol::SSLRequest, upgrades the socket
// to TLS and re-reads the full HandshakeResponse from the encrypted stream
// before delegating to the templated post-greeting body.
[[nodiscard]] boost::asio::awaitable<void> session(
    boost::asio::ip::tcp::socket socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &username,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &password,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &server_rsa_public_key_path,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &server_rsa_private_key_path,
    minimysql::ssl_acceptor_context *ssl_ctx) {
  boost::system::error_code session_ec;
  const auto remote_endpoint{socket.remote_endpoint(session_ec)};

  const scope_tracer tracer("session " +
                            boost::lexical_cast<std::string>(remote_endpoint));

  try {
    minimysql::network_buffer_type data;
    data.reserve(network_service::expected_packet_size);

    minimysql::connection_context context{
        username, password, server_rsa_public_key_path,
        server_rsa_private_key_path,
        /* ssl_capability_enabled = */ ssl_ctx != nullptr};

    const auto server_greeting{context.generate_encoded_server_greeting()};
    print_server_greeting(remote_endpoint, context);
    co_await minimysql::async_write_mysql_frame(
        socket, server_greeting,
        network_service::session_authentication_timeout);
    std::cout << "sent server greeting (" << std::size(server_greeting)
              << " bytes to " << remote_endpoint << ")\n";

    co_await minimysql::async_read_mysql_frame(
        socket, data, network_service::session_authentication_timeout);
    std::cout << "received client greeting (" << std::size(data)
              << " bytes from " << remote_endpoint << ")\n";
    context.parse_client_greeting(data);
    print_client_greeting(remote_endpoint, context);

    // Reject an SSL-requesting client the same way Percona Server does when
    // its own SSL acceptor context is missing (see
    // sql/auth/sql_authentication.cc: `if (!context.have_ssl()) return
    // packet_error;`): drop the connection without sending an error frame,
    // and log the reason for the operator. parse_client_greeting() is
    // lenient enough to decode both the full form and the truncated
    // SSLRequest form regardless of what the server advertised, so this
    // decision is made after we have a fully populated context to inspect.
    if (ssl_ctx == nullptr && context.client_requested_ssl()) {
      std::cout << "client " << remote_endpoint
                << " requested SSL (CLIENT_SSL capability bit set) but the "
                   "server has no SSL context configured; start "
                   "minimysql_server with --ssl-cert=<path> --ssl-key=<path> "
                   "to enable TLS. Closing connection (matches Percona "
                   "Server behaviour: no error frame is sent mid-handshake).\n";
      co_return;
    }

    if (ssl_ctx != nullptr && context.is_sslrequest_greeting()) {
      std::cout << "client requested TLS upgrade (SSLRequest) from "
                << remote_endpoint << '\n';

      boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket{
          std::move(socket), ssl_ctx->native()};

      co_await perform_ssl_handshake(
          ssl_socket, network_service::session_authentication_timeout);

      context.mark_transport_secure();
      std::cout << "TLS handshake completed with " << remote_endpoint << '\n';

      co_await minimysql::async_read_mysql_frame(
          ssl_socket, data, network_service::session_authentication_timeout);
      std::cout << "received encrypted client greeting (" << std::size(data)
                << " bytes from " << remote_endpoint << ")\n";
      context.parse_client_greeting(data);
      print_client_greeting(remote_endpoint, context);

      co_await session_body(ssl_socket, context, remote_endpoint, data);
    } else {
      co_await session_body(socket, context, remote_endpoint, data);
    }
  } catch (...) {
    const std::string context{
        "session " + boost::lexical_cast<std::string>(remote_endpoint)};
    handle_exception(context);
  }
}
#pragma GCC diagnostic pop

// listener coroutine - accepts incoming connections and spawns a session
// coroutine for each accepted connection
[[nodiscard]] boost::asio::awaitable<void> listener(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::acceptor &acceptor,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &username,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &password,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &server_rsa_public_key_path,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &server_rsa_private_key_path,
    minimysql::ssl_acceptor_context *ssl_ctx) {
  const scope_tracer tracer("listener");

  auto executor = acceptor.get_executor();

  try {
    for (;;) {
      boost::system::error_code listener_ec;
      boost::asio::ip::tcp::socket socket = co_await acceptor.async_accept(
          boost::asio::redirect_error(boost::asio::use_awaitable, listener_ec));
      if (listener_ec) {
        if (listener_ec != boost::asio::error::operation_aborted &&
            listener_ec != boost::asio::error::bad_descriptor) {
          throw boost::system::system_error{listener_ec};
        }
        std::cout << "listener stopped\n";
        break;
      }

      const auto remote_endpoint{socket.remote_endpoint(listener_ec)};
      std::cout << "accepted connection from " << remote_endpoint << '\n';

      // NOLINTNEXTLINE(misc-include-cleaner)
      boost::asio::co_spawn(executor,
                            session(std::move(socket), username, password,
                                    server_rsa_public_key_path,
                                    server_rsa_private_key_path, ssl_ctx),
                            boost::asio::detached);
    }
  } catch (...) {
    handle_exception("listener");
  }
}

} // anonymous namespace

network_service::network_service(
    boost::asio::io_context &context, std::uint16_t listening_port,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view username, std::string_view password,
    std::string_view server_rsa_public_key_path,
    std::string_view server_rsa_private_key_path,
    std::unique_ptr<ssl_acceptor_context> ssl_ctx)
    : username_(username), password_(password),
      server_rsa_public_key_path_{server_rsa_public_key_path},
      server_rsa_private_key_path_{server_rsa_private_key_path},
      context_{&context}, ssl_ctx_{std::move(ssl_ctx)},
      acceptor_{std::make_unique<acceptor_type>(
          context, boost::asio::ip::tcp::endpoint{boost::asio::ip::tcp::v4(),
                                                  listening_port})} {
  // NOLINTNEXTLINE(misc-include-cleaner)
  boost::asio::co_spawn(*context_,
                        listener(*acceptor_, username_, password_,
                                 server_rsa_public_key_path_,
                                 server_rsa_private_key_path_, ssl_ctx_.get()),
                        boost::asio::detached);
}

network_service::~network_service() = default;

} // namespace minimysql
