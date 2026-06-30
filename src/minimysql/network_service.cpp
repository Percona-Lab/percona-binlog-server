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
#include <boost/asio/co_spawn.hpp> // IWYU pragma: keep
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"

#include <boost/asio/io_context.hpp>

#pragma GCC diagnostic pop

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <boost/describe/enum_to_string.hpp>

#include <boost/system/system_error.hpp>

#include "minimysql/connection_context.hpp"
#include "minimysql/network_io_operations.hpp"
#include "minimysql/sample_event_collection.hpp"

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

// MySQL session handling coroutine - writes server greeting, then receives and
// parses client greeting
[[nodiscard]] boost::asio::awaitable<void> session(
    boost::asio::ip::tcp::socket socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &username,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &password) {
  boost::system::error_code session_ec;
  const auto remote_endpoint{socket.remote_endpoint(session_ec)};

  const scope_tracer tracer("session " +
                            boost::lexical_cast<std::string>(remote_endpoint));

  try {
    minimysql::network_buffer_type data;
    data.reserve(network_service::expected_packet_size);

    minimysql::connection_context context{username, password};

    // creating and sending server greeting packet:
    //   protocol_version: 10
    //   server_version: "9.7.0-pbs",
    //   connection_id: <counter> (maintained by connection_context, starts with
    //   1 and is incremented for each new connection) auth_method_data: 20
    //   random bytes generated by connection_context server_capabilities:
    //   collation: 0 (not set explicitly, client will assume the default one,
    //   most probably 255 utf8mb4_0900_ai_ci) status_flags: 0 auth_method:
    //   "caching_sha2_password"

    const auto server_greeting{context.generate_encoded_server_greeting()};
    print_server_greeting(remote_endpoint, context);
    co_await minimysql::async_write_mysql_frame(
        socket, server_greeting,
        network_service::session_authentication_timeout);
    std::cout << "sent server greeting (" << std::size(server_greeting)
              << " bytes to " << remote_endpoint << ")\n";

    // receiving and parsing client greeting packet:
    //   capabilities
    //   max_packet_size
    //   collation
    //   username
    //   auth_method_data
    //   schema
    //   auth_method_name
    //   attributes

    co_await minimysql::async_read_mysql_frame(
        socket, data, network_service::session_authentication_timeout);
    std::cout << "received client greeting (" << std::size(data)
              << " bytes from " << remote_endpoint << ")\n";
    context.parse_client_greeting(data);
    print_client_greeting(remote_endpoint, context);

    if (!context.check_shared_plugin_auth_supported()) {
      std::cout << "client does not support plugin authentication\n";
      const auto access_denied{context.generate_encoded_access_denied()};
      print_error(remote_endpoint, context, "plugin auth required");
      co_await minimysql::async_write_mysql_frame(
          socket, access_denied,
          network_service::session_authentication_timeout);
      std::cout << "sent server access denied (" << std::size(access_denied)
                << " bytes to " << remote_endpoint << ")\n";
      co_return;
    }

    if (context.get_client_auth_method() != context.get_server_auth_method()) {
      std::cout << "client requested " << context.get_client_auth_method()
                << " authentication that does not match the one associated "
                   "with the user account ("
                << context.get_server_auth_method() << ")\n";
      // TODO: send SwitchAuthentication packet
      co_return;
    }
    if (!context.check_client_authentication()) {
      std::cout << "client authentication failed for "
                << context.get_client_username() << '\n';
      const auto access_denied{context.generate_encoded_access_denied()};
      print_error(remote_endpoint, context, "auth failure");
      co_await minimysql::async_write_mysql_frame(
          socket, access_denied,
          network_service::session_authentication_timeout);
      std::cout << "sent server access denied (" << std::size(access_denied)
                << " bytes to " << remote_endpoint << ")\n";
      co_return;
    }

    std::cout << "client authentication succeeded for "
              << context.get_client_username() << '\n';

    // sending fast auth success
    const auto fast_auth_success{context.generate_encoded_fast_auth()};
    print_generic(remote_endpoint, context, "auth method data (fast auth)");
    co_await minimysql::async_write_mysql_frame(
        socket, fast_auth_success,
        network_service::session_authentication_timeout);
    std::cout << "sent server fast auth success ("
              << std::size(fast_auth_success) << " bytes to " << remote_endpoint
              << ")\n";

    // sending server ok after successful authentication
    const auto auth_ok{context.generate_encoded_ok()};
    print_generic(remote_endpoint, context, "ok (auth)");
    co_await minimysql::async_write_mysql_frame(
        socket, auth_ok, network_service::session_authentication_timeout);
    std::cout << "sent server ok after authentication (" << std::size(auth_ok)
              << " bytes to " << remote_endpoint << ")\n";

    // defining known queries container
    using query_handler_type =
        std::function<minimysql::network_buffer_container(
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
          // co_await minimysql::async_read_mysql_frame(socket, data,
          // network_service::session_command_timeout); std::cout << "received
          // binlog event reply command (" << std::size(data) << " bytes from "
          // << remote_endpoint
          // << ")\n";
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
    const std::string &password) {
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
                            session(std::move(socket), username, password),
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
    std::string_view username, std::string_view password)
    : username_(username), password_(password), context_{&context},
      acceptor_{std::make_unique<acceptor_type>(
          context, boost::asio::ip::tcp::endpoint{boost::asio::ip::tcp::v4(),
                                                  listening_port})} {
  // NOLINTNEXTLINE(misc-include-cleaner)
  boost::asio::co_spawn(*context_, listener(*acceptor_, username_, password_),
                        boost::asio::detached);
}

network_service::~network_service() = default;

} // namespace minimysql
