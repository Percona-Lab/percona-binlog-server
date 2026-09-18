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
#include <format>
#include <functional>
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

#include "binsrv/basic_logger.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/storage.hpp"

#include "minimysql/connection_context.hpp"
#include "minimysql/network_io_operations.hpp"
#include "minimysql/sample_event_collection.hpp"

#include "util/byte_span.hpp"

namespace minimysql {

namespace {

class scope_tracer {
public:
  scope_tracer(binsrv::basic_logger &logger, std::string_view name)
      : logger_{&logger}, name_(name) {
    assert(logger_ != nullptr);
    logger_->log_format(binsrv::log_severity::trace, "net    : entering {}",
                        name_);
  }
  scope_tracer(const scope_tracer &) = delete;
  scope_tracer &operator=(const scope_tracer &) = delete;
  scope_tracer(scope_tracer &&) = delete;
  scope_tracer &operator=(scope_tracer &&) = delete;

  ~scope_tracer() {
    // logging must not throw from a destructor
    try {
      logger_->log_format(binsrv::log_severity::trace, "net    : leaving {}",
                          name_);
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
  }

private:
  binsrv::basic_logger *logger_;
  std::string name_;
};

void print_server_greeting(
    binsrv::basic_logger &logger,
    const boost::asio::ip::tcp::endpoint &remote_endpoint,
    const minimysql::connection_context &context) {
  logger.log_format(binsrv::log_severity::info,
                    "net    : generated server greeting for {}",
                    boost::lexical_cast<std::string>(remote_endpoint));
  logger.log_format(
      binsrv::log_severity::debug,
      "net    : frame sequence_number {:d}\n"
      "  protocol_version: {}\n"
      "  server_version  : {}\n"
      "  connection_id   : {}\n"
      "  collation       : {}\n"
      "  status flags    : {}\n"
      "  auth_method     : {}\n"
      "  auth_method_data: {} byte(s)",
      context.get_last_sequence_number(),
      minimysql::connection_context::default_server_protocol_version,
      minimysql::connection_context::default_server_version,
      context.get_connection_id(),
      minimysql::connection_context::default_server_collation,
      minimysql::connection_context::default_server_status_flags,
      context.get_server_auth_method(),
      std::size(context.get_server_auth_method_data()));
}

void print_client_greeting(
    binsrv::basic_logger &logger,
    const boost::asio::ip::tcp::endpoint &remote_endpoint,
    const minimysql::connection_context &context) {
  logger.log_format(binsrv::log_severity::info,
                    "net    : parsed client greeting from {}",
                    boost::lexical_cast<std::string>(remote_endpoint));
  logger.log_format(
      binsrv::log_severity::debug,
      "net    : frame sequence_number {:d}\n"
      "  auth_method     : {}\n"
      "  auth_method_data: {} byte(s)\n"
      "  username        : {}\n"
      "  schema          : {}\n"
      "  collation       : {}\n"
      "  max_packet_size : {}",
      context.get_last_sequence_number(), context.get_client_auth_method(),
      std::size(context.get_client_auth_method_data()),
      context.get_client_username(), context.get_client_schema(),
      context.get_client_collation(), context.get_client_max_packet_size());
}

void print_client_auth_method_switch(
    binsrv::basic_logger &logger,
    const boost::asio::ip::tcp::endpoint &remote_endpoint,
    const minimysql::connection_context &context) {
  logger.log_format(binsrv::log_severity::info,
                    "net    : parsed client auth method switch from {}",
                    boost::lexical_cast<std::string>(remote_endpoint));
  logger.log_format(binsrv::log_severity::debug,
                    "net    : frame sequence_number {:d}\n"
                    "  auth_method: {}\n"
                    "  auth_method_data: {} byte(s)",
                    context.get_last_sequence_number(),
                    context.get_client_auth_method(),
                    std::size(context.get_client_auth_method_data()));
}

void print_generic(binsrv::basic_logger &logger,
                   const boost::asio::ip::tcp::endpoint &remote_endpoint,
                   const minimysql::connection_context &context,
                   std::string_view label) {
  logger.log_format(binsrv::log_severity::info,
                    "net    : generated {} packet for {}", label,
                    boost::lexical_cast<std::string>(remote_endpoint));
  logger.log_format(binsrv::log_severity::debug,
                    "net    : frame sequence_number {:d}",
                    context.get_last_sequence_number());
}

void print_error(binsrv::basic_logger &logger,
                 const boost::asio::ip::tcp::endpoint &remote_endpoint,
                 const minimysql::connection_context &context,
                 std::string_view label) {
  logger.log_format(binsrv::log_severity::info,
                    "net    : generated error packet ({}) for {}", label,
                    boost::lexical_cast<std::string>(remote_endpoint));
  logger.log_format(binsrv::log_severity::debug,
                    "net    : frame sequence_number {:d}",
                    context.get_last_sequence_number());
}

void print_client_command(binsrv::basic_logger &logger,
                          const boost::asio::ip::tcp::endpoint &remote_endpoint,
                          const minimysql::connection_context &context) {
  logger.log_format(binsrv::log_severity::info,
                    "net    : parsed client command from {}",
                    boost::lexical_cast<std::string>(remote_endpoint));

  std::string command_details;
  if (context.get_client_mysql_command() ==
      minimysql::client_command_type::query) {
    command_details =
        std::format("\n  statement       : {}", context.get_client_statement());
  }
  if (context.get_client_mysql_command() ==
      minimysql::client_command_type::binlog_dump) {
    command_details = std::format(
        "\n  binlog blocking mode : {}"
        "\n  binlog server id     : {}"
        "\n  binlog file name     : {}"
        "\n  binlog position      : {}",
        (context.check_binlog_non_blocking_dump() ? "non-blocking"
                                                  : "blocking"),
        context.get_binlog_server_id(), context.get_binlog_filename(),
        context.get_binlog_position());
  }
  logger.log_format(binsrv::log_severity::debug,
                    "net    : frame sequence_number {:d}\n"
                    "  code            : {}{}",
                    context.get_last_sequence_number(),
                    boost::describe::enum_to_string(
                        context.get_client_mysql_command(), "unknown"),
                    command_details);
}

// a helper function to log exceptions with context information
void handle_exception(binsrv::basic_logger &logger, std::string_view context) {
  try {
    throw;
  } catch (const boost::system::system_error &e) {
    bool logged_specific_error{false};
    const auto exception_error_code{e.code()};
    if (exception_error_code.category() ==
        boost::asio::error::get_system_category()) {
      switch (exception_error_code.value()) {
      case boost::asio::error::timed_out:
        logger.log_format(binsrv::log_severity::info, "net    : {}: {}",
                          context, e.what());
        logged_specific_error = true;
        break;
      case boost::asio::error::operation_aborted:
        logger.log_format(binsrv::log_severity::info,
                          "net    : {}: operation aborted", context);
        logged_specific_error = true;
        break;
      case boost::asio::error::eof:
      case boost::asio::error::connection_reset:
        logger.log_format(binsrv::log_severity::info,
                          "net    : {}: connection closed by peer", context);
        logged_specific_error = true;
        break;
      default:
        // for other system errors we log the error code and message and rethrow
        break;
      }
    }

    if (!logged_specific_error) {
      logger.log_format(binsrv::log_severity::error,
                        "net    : system error caught in {}: {}\n  {}", context,
                        boost::lexical_cast<std::string>(exception_error_code),
                        e.what());
    }
  } catch (const std::exception &e) {
    logger.log_format(binsrv::log_severity::error,
                      "net    : exception caught in {}: {}", context, e.what());
  } catch (...) {
    logger.log_format(binsrv::log_severity::error,
                      "net    : unknown exception caught in {}", context);
  }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"

// MySQL session handling coroutine - writes server greeting, then receives and
// parses client greeting
[[nodiscard]] boost::asio::awaitable<void> session(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    binsrv::basic_logger &logger,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    binsrv::storage &storage, boost::asio::ip::tcp::socket socket,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::chrono::seconds read_timeout, std::chrono::seconds write_timeout,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &username,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &password) {
  boost::system::error_code session_ec;
  const auto remote_endpoint{socket.remote_endpoint(session_ec)};
  const auto remote_endpoint_str{
      boost::lexical_cast<std::string>(remote_endpoint)};

  const scope_tracer tracer(logger, "session " + remote_endpoint_str);

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
    print_server_greeting(logger, remote_endpoint, context);
    co_await minimysql::async_write_mysql_frame(socket, server_greeting,
                                                write_timeout);
    logger.log_format(binsrv::log_severity::debug,
                      "net    : sent server greeting ({} bytes to {})",
                      std::size(server_greeting), remote_endpoint_str);

    // receiving and parsing client greeting packet:
    //   capabilities
    //   max_packet_size
    //   collation
    //   username
    //   auth_method_data
    //   schema
    //   auth_method_name
    //   attributes

    co_await minimysql::async_read_mysql_frame(socket, data, read_timeout);
    logger.log_format(binsrv::log_severity::debug,
                      "net    : received client greeting ({} bytes from {})",
                      std::size(data), remote_endpoint_str);
    context.parse_client_greeting(data);
    print_client_greeting(logger, remote_endpoint, context);

    if (!context.check_shared_plugin_auth_supported()) {
      logger.log(binsrv::log_severity::warning,
                 "net    : client does not support plugin authentication");
      const auto access_denied{context.generate_encoded_access_denied()};
      print_error(logger, remote_endpoint, context, "plugin auth required");
      co_await minimysql::async_write_mysql_frame(socket, access_denied,
                                                  write_timeout);
      logger.log_format(binsrv::log_severity::debug,
                        "net    : sent server access denied ({} bytes to {})",
                        std::size(access_denied), remote_endpoint_str);
      co_return;
    }

    if (context.get_client_auth_method() != context.get_server_auth_method()) {
      logger.log_format(binsrv::log_severity::info,
                        "net    : client requested {} authentication that does "
                        "not match the one "
                        "associated with the user account ({})",
                        context.get_client_auth_method(),
                        context.get_server_auth_method());

      const auto auth_method_switch{
          context.generate_encoded_auth_method_switch()};
      print_generic(logger, remote_endpoint, context, "auth method switch");
      co_await minimysql::async_write_mysql_frame(socket, auth_method_switch,
                                                  write_timeout);
      logger.log_format(
          binsrv::log_severity::debug,
          "net    : sent server auth method switch ({} bytes to {})",
          std::size(auth_method_switch), remote_endpoint_str);

      co_await minimysql::async_read_mysql_frame(socket, data, read_timeout);
      logger.log_format(binsrv::log_severity::debug,
                        "net    : received client auth method switch response "
                        "({} bytes from {})",
                        std::size(data), remote_endpoint_str);
      context.parse_client_auth_method_switch(data);
      print_client_auth_method_switch(logger, remote_endpoint, context);
    }
    if (!context.check_client_authentication()) {
      logger.log_format(binsrv::log_severity::warning,
                        "net    : client authentication failed for {}",
                        context.get_client_username());
      const auto access_denied{context.generate_encoded_access_denied()};
      print_error(logger, remote_endpoint, context, "auth failure");
      co_await minimysql::async_write_mysql_frame(socket, access_denied,
                                                  write_timeout);
      logger.log_format(binsrv::log_severity::debug,
                        "net    : sent server access denied ({} bytes to {})",
                        std::size(access_denied), remote_endpoint_str);
      co_return;
    }

    logger.log_format(binsrv::log_severity::info,
                      "net    : client authentication succeeded for {}",
                      context.get_client_username());

    // sending fast auth success
    const auto fast_auth_success{context.generate_encoded_fast_auth()};
    print_generic(logger, remote_endpoint, context,
                  "auth method data (fast auth)");
    co_await minimysql::async_write_mysql_frame(socket, fast_auth_success,
                                                write_timeout);
    logger.log_format(binsrv::log_severity::debug,
                      "net    : sent server fast auth success ({} bytes to {})",
                      std::size(fast_auth_success), remote_endpoint_str);

    // sending server ok after successful authentication
    const auto auth_ok{context.generate_encoded_ok()};
    print_generic(logger, remote_endpoint, context, "ok (auth)");
    co_await minimysql::async_write_mysql_frame(socket, auth_ok, write_timeout);
    logger.log_format(
        binsrv::log_severity::debug,
        "net    : sent server ok after authentication ({} bytes to {})",
        std::size(auth_ok), remote_endpoint_str);

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
      co_await minimysql::async_read_mysql_frame(socket, data, read_timeout);
      logger.log_format(binsrv::log_severity::debug,
                        "net    : received client command ({} bytes from {})",
                        std::size(data), remote_endpoint_str);
      context.parse_client_command(data);
      print_client_command(logger, remote_endpoint, context);

      switch (context.get_client_mysql_command()) {
      case minimysql::client_command_type::query: {
        const auto known_query_it{
            known_queries.find(context.get_client_statement())};
        if (known_query_it != std::end(known_queries)) {
          const auto resultset{known_query_it->second(context)};
          print_generic(logger, remote_endpoint, context, "resultset");
          co_await minimysql::async_write_mysql_frames(socket, resultset,
                                                       write_timeout);
          logger.log_format(binsrv::log_severity::debug,
                            "net    : sent server resultset ({} frames to {})",
                            std::size(resultset), remote_endpoint_str);
        } else {
          // return 'syntax error' for every other query
          const auto syntax_error = context.generate_encoded_syntax_error();
          print_error(logger, remote_endpoint, context, "syntax error");
          co_await minimysql::async_write_mysql_frame(socket, syntax_error,
                                                      write_timeout);
          logger.log_format(
              binsrv::log_severity::debug,
              "net    : sent server syntax error ({} bytes to {})",
              std::size(syntax_error), remote_endpoint_str);
        }
      } break;
      case minimysql::client_command_type::ping: {
        const auto ok_after_ping{context.generate_encoded_ok()};
        print_generic(logger, remote_endpoint, context, "ok (ping success)");
        co_await minimysql::async_write_mysql_frame(socket, ok_after_ping,
                                                    write_timeout);
        logger.log_format(binsrv::log_severity::debug,
                          "net    : sent server ok after ping ({} bytes to {})",
                          std::size(ok_after_ping), remote_endpoint_str);
      } break;
      case minimysql::client_command_type::binlog_dump: {
        const minimysql::sample_event_collection sample_events;
        // TODO: rework with reading real data from storage;
        (void)storage;
        for (const auto &event_data : sample_events.get_events()) {
          const auto event{context.generate_encoded_binlog_event(
              util::as_const_byte_span(event_data))};
          print_generic(logger, remote_endpoint, context, "binlog event");
          co_await minimysql::async_write_mysql_frame(socket, event,
                                                      write_timeout);
          logger.log_format(
              binsrv::log_severity::debug,
              "net    : sent server binlog event ({} bytes to {})",
              std::size(event), remote_endpoint_str);
        }
        const auto eof = context.generate_encoded_eof();
        print_generic(logger, remote_endpoint, context, "binlog eof");
        co_await minimysql::async_write_mysql_frame(socket, eof, write_timeout);
        logger.log_format(binsrv::log_severity::debug,
                          "net    : sent server eof ({} bytes to {})",
                          std::size(eof), remote_endpoint_str);
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
        print_error(logger, remote_endpoint, context, "unknown command");
        co_await minimysql::async_write_mysql_frame(
            socket, unknown_command_error, write_timeout);
        logger.log_format(
            binsrv::log_severity::debug,
            "net    : sent server unknown command ({} bytes to {})",
            std::size(unknown_command_error), remote_endpoint_str);
      }
      }
    }
  } catch (...) {
    handle_exception(logger, "session " + remote_endpoint_str);
  }
}
#pragma GCC diagnostic pop

// listener coroutine - accepts incoming connections and spawns a session
// coroutine for each accepted connection
[[nodiscard]] boost::asio::awaitable<void> listener(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    binsrv::basic_logger &logger,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    binsrv::storage &storage,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::acceptor &acceptor,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::chrono::seconds read_timeout, std::chrono::seconds write_timeout,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &username,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const std::string &password) {
  const scope_tracer tracer(logger, "listener");

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
        logger.log(binsrv::log_severity::info, "net    : listener stopped");
        break;
      }

      const auto remote_endpoint{socket.remote_endpoint(listener_ec)};
      logger.log_format(binsrv::log_severity::info,
                        "net    : accepted connection from {}",
                        boost::lexical_cast<std::string>(remote_endpoint));

      // NOLINTNEXTLINE(misc-include-cleaner)
      boost::asio::co_spawn(executor,
                            session(logger, storage, std::move(socket),
                                    read_timeout, write_timeout, username,
                                    password),
                            boost::asio::detached);
    }
  } catch (...) {
    handle_exception(logger, "listener");
  }
}

} // anonymous namespace

network_service::network_service(
    binsrv::basic_logger_ptr logger, boost::asio::io_context &context,
    binsrv::storage_ptr storage, std::uint16_t listening_port,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::chrono::seconds read_timeout, std::chrono::seconds write_timeout,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view username, std::string_view password)
    : logger_{std::move(logger)}, storage_{std::move(storage)},
      username_(username), password_(password), context_{&context},
      acceptor_{std::make_unique<acceptor_type>(
          context, boost::asio::ip::tcp::endpoint{boost::asio::ip::tcp::v4(),
                                                  listening_port})} {
  assert(logger_);
  // NOLINTNEXTLINE(misc-include-cleaner)
  boost::asio::co_spawn(*context_,
                        listener(*logger_, *storage_, *acceptor_, read_timeout,
                                 write_timeout, username_, password_),
                        boost::asio::detached);
}

network_service::~network_service() = default;

} // namespace minimysql
