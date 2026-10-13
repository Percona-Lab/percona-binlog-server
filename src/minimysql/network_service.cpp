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

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
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
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <boost/describe/enum_to_string.hpp>

#include <boost/system/system_error.hpp>

#include "binsrv/authentication_config.hpp"
#include "binsrv/basic_logger.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/replication_mode_type.hpp"
#include "binsrv/replication_source_config.hpp"
#include "binsrv/storage.hpp"

#include "minimysql/connection_context.hpp"
#include "minimysql/network_io_operations.hpp"

#include "opensslpp/evp_pkey.hpp"

#include "operations/sender_context.hpp"

#include "util/byte_span.hpp"
#include "util/common_optional_types.hpp"
#include "util/file_operations_helpers.hpp"

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

} // anonymous namespace

void network_service::print_server_greeting(std::string_view remote_endpoint,
                                            const connection_context &context) {
  logger_->log_format(binsrv::log_severity::info,
                      "net    : generated server greeting for {}",
                      remote_endpoint);
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : frame sequence_number {:d}\n"
                      "  protocol_version: {}\n"
                      "  server_version  : {}\n"
                      "  connection_id   : {}\n"
                      "  collation       : {}\n"
                      "  status flags    : {}\n"
                      "  auth_method     : {}\n"
                      "  auth_method_data: {} byte(s)",
                      context.get_last_sequence_number(),
                      connection_context::default_server_protocol_version,
                      connection_context::default_server_version,
                      context.get_connection_id(),
                      connection_context::default_server_collation,
                      connection_context::default_server_status_flags,
                      context.get_server_auth_method(),
                      std::size(context.get_server_auth_method_data()));
}

void network_service::print_client_greeting(std::string_view remote_endpoint,
                                            const connection_context &context) {
  logger_->log_format(binsrv::log_severity::info,
                      "net    : parsed client greeting from {}",
                      remote_endpoint);
  logger_->log_format(
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

void network_service::print_client_auth_method_switch(
    std::string_view remote_endpoint, const connection_context &context) {
  logger_->log_format(binsrv::log_severity::info,
                      "net    : parsed client auth method switch from {}",
                      remote_endpoint);
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : frame sequence_number {:d}\n"
                      "  auth_method: {}\n"
                      "  auth_method_data: {} byte(s)",
                      context.get_last_sequence_number(),
                      context.get_client_auth_method(),
                      std::size(context.get_client_auth_method_data()));
}

void network_service::print_generic(std::string_view remote_endpoint,
                                    const connection_context &context,
                                    std::string_view label) {
  logger_->log_format(binsrv::log_severity::info,
                      "net    : generated {} packet for {}", label,
                      remote_endpoint);
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : frame sequence_number {:d}",
                      context.get_last_sequence_number());
}

void network_service::print_error(std::string_view remote_endpoint,
                                  const connection_context &context,
                                  std::string_view label) {
  logger_->log_format(binsrv::log_severity::info,
                      "net    : generated error packet ({}) for {}", label,
                      remote_endpoint);
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : frame sequence_number {:d}",
                      context.get_last_sequence_number());
}

void network_service::print_client_command(std::string_view remote_endpoint,
                                           const connection_context &context) {
  logger_->log_format(binsrv::log_severity::info,
                      "net    : parsed client command from {}",
                      remote_endpoint);

  std::string command_details;
  if (context.get_client_mysql_command() == client_command_type::query) {
    command_details =
        std::format("\n  statement       : {}", context.get_client_statement());
  }
  if (context.get_client_mysql_command() == client_command_type::binlog_dump) {
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
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : frame sequence_number {:d}\n"
                      "  code            : {}{}",
                      context.get_last_sequence_number(),
                      boost::describe::enum_to_string(
                          context.get_client_mysql_command(), "unknown"),
                      command_details);
}

// a helper function to log exceptions with context information
void network_service::handle_exception(std::string_view context) {
  try {
    throw;
  } catch (const boost::system::system_error &e) {
    bool logged_specific_error{false};
    const auto exception_error_code{e.code()};
    if (exception_error_code.category() ==
        boost::asio::error::get_system_category()) {
      switch (exception_error_code.value()) {
      case boost::asio::error::timed_out:
        logger_->log_format(binsrv::log_severity::info, "net    : {}: {}",
                            context, e.what());
        logged_specific_error = true;
        break;
      case boost::asio::error::operation_aborted:
        logger_->log_format(binsrv::log_severity::info,
                            "net    : {}: operation aborted", context);
        logged_specific_error = true;
        break;
      case boost::asio::error::eof:
      case boost::asio::error::connection_reset:
        logger_->log_format(binsrv::log_severity::info,
                            "net    : {}: connection closed by peer", context);
        logged_specific_error = true;
        break;
      default:
        // for other system errors we log the error code and message and rethrow
        break;
      }
    }

    if (!logged_specific_error) {
      logger_->log_format(
          binsrv::log_severity::error,
          "net    : system error caught in {}: {}\n  {}", context,
          boost::lexical_cast<std::string>(exception_error_code), e.what());
    }
  } catch (const std::exception &e) {
    logger_->log_format(binsrv::log_severity::error,
                        "net    : exception caught in {}: {}", context,
                        e.what());
  } catch (...) {
    logger_->log_format(binsrv::log_severity::error,
                        "net    : unknown exception caught in {}", context);
  }
}

[[nodiscard]] bool network_service::check_if_in_auth_cache(
    const connection_context & /*context*/) {
  // TODO: implement a real caching_sha2_password authentication cache (MySQL
  // Server remembers users who have successfully passed full authentication
  // and lets them use the faster scramble-based authentication afterwards)
  return false;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"

[[nodiscard]] boost::asio::awaitable<void>
network_service::handle_binlog_dump_command(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    connection_context &context, bool session_source_binlog_checksum) {
  static constexpr auto block_size{1048576UZ};
  static constexpr auto blocking_poll_interval{std::chrono::milliseconds{500}};

  const auto remote_endpoint_str{extract_remote_endpoint_str(socket)};

  const std::chrono::seconds write_timeout{get_config().get<"write_timeout">()};

  const bool blocking{!context.check_binlog_non_blocking_dump()};

  const auto &binlog_name{context.get_binlog_filename()};
  const auto position{context.get_binlog_position()};

  operations::sender_context sender_ctx{
      logger_,     storage_, block_size,
      binlog_name, position, session_source_binlog_checksum};
  boost::asio::steady_timer idle_timer{
      co_await boost::asio::this_coro::executor};
  util::const_byte_span event_data{};
  for (;;) {
    if (!sender_ctx.get_event(event_data)) {
      logger_->log_format(binsrv::log_severity::error,
                          "net    : failed to fetch next event block for {}",
                          remote_endpoint_str);
      co_return;
    }
    if (event_data.empty()) {
      if (!blocking) {
        break;
      }
      idle_timer.expires_after(blocking_poll_interval);
      co_await idle_timer.async_wait(boost::asio::use_awaitable);
      continue;
    }
    const auto event_frame{context.generate_encoded_binlog_event(event_data)};
    print_generic(remote_endpoint_str, context, "binlog event");
    co_await async_write_mysql_frame(socket, event_frame, write_timeout);
    logger_->log_format(binsrv::log_severity::debug,
                        "net    : sent server binlog event ({} bytes to {})",
                        std::size(event_frame), remote_endpoint_str);
  }

  const auto eof = context.generate_encoded_eof();
  print_generic(remote_endpoint_str, context, "binlog eof");
  co_await async_write_mysql_frame(socket, eof, write_timeout);
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : sent server eof ({} bytes to {})",
                      std::size(eof), remote_endpoint_str);
}

[[nodiscard]] boost::asio::awaitable<bool>
network_service::perform_fast_authentication(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    connection_context &context) {
  network_buffer_type data;
  data.reserve(network_service::expected_packet_size);

  const auto remote_endpoint_str{extract_remote_endpoint_str(socket)};

  const std::chrono::seconds write_timeout{get_config().get<"write_timeout">()};

  // fast authentication: the scramble received in the client greeting
  // (or in the auth method switch response) is verified directly
  if (!context.check_client_fast_authentication()) {
    co_return false;
  }
  // sending fast auth success
  const auto fast_auth_success{context.generate_encoded_fast_auth()};
  print_generic(remote_endpoint_str, context, "auth method data (fast auth)");
  co_await async_write_mysql_frame(socket, fast_auth_success, write_timeout);
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : sent server fast auth success ({} bytes to {})",
                      std::size(fast_auth_success), remote_endpoint_str);

  co_return true;
}

[[nodiscard]] boost::asio::awaitable<void>
network_service::perform_public_key_dispatch(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    connection_context &context) {
  network_buffer_type data;
  data.reserve(network_service::expected_packet_size);

  const auto remote_endpoint_str{extract_remote_endpoint_str(socket)};

  const std::chrono::seconds read_timeout{get_config().get<"read_timeout">()};
  const std::chrono::seconds write_timeout{get_config().get<"write_timeout">()};

  // the client (e.g. 'mysql --get-server-public-key') does not have the
  // server's RSA public key and asks for it - sending it in PEM format,
  // after that the client is expected to respond with the encrypted
  // password
  // the public key is sent only once - if the client requests it again,
  // this request will be treated as an encrypted password of invalid size
  const auto public_key{
      context.generate_encoded_public_key(auth_rsa_public_key_pem_)};
  print_generic(remote_endpoint_str, context, "auth method data (public key)");
  co_await async_write_mysql_frame(socket, public_key, write_timeout);
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : sent server RSA public key ({} bytes to {})",
                      std::size(public_key), remote_endpoint_str);

  co_await async_read_mysql_frame(socket, data, read_timeout);
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : received client full auth response after "
                      "public key ({} bytes from {})",
                      std::size(data), remote_endpoint_str);
  context.parse_client_full_auth_response(data);
}

// caching_sha2_password "full authentication" handling coroutine - sends
// "perform full authentication" packet, receives the client's password
// encrypted with the server's RSA public key (optionally sending this public
// key to the client first if requested) and verifies it
[[nodiscard]] boost::asio::awaitable<bool>
network_service::perform_full_authentication(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    connection_context &context) {
  network_buffer_type data;
  data.reserve(network_service::expected_packet_size);

  const auto remote_endpoint_str{extract_remote_endpoint_str(socket)};

  const std::chrono::seconds read_timeout{get_config().get<"read_timeout">()};
  const std::chrono::seconds write_timeout{get_config().get<"write_timeout">()};

  // full authentication: the client has to send the password itself and,
  // as the connection is not encrypted, the password is expected to be
  // encrypted with the server's RSA public key
  const auto full_auth_required{context.generate_encoded_full_auth()};
  print_generic(remote_endpoint_str, context, "auth method data (full auth)");
  co_await async_write_mysql_frame(socket, full_auth_required, write_timeout);
  logger_->log_format(
      binsrv::log_severity::debug,
      "net    : sent server full auth required ({} bytes to {})",
      std::size(full_auth_required), remote_endpoint_str);

  co_await async_read_mysql_frame(socket, data, read_timeout);
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : received client full auth response "
                      "({} bytes from {})",
                      std::size(data), remote_endpoint_str);
  context.parse_client_full_auth_response(data);

  // just like MySQL Server, checking for the RSA key availability before
  // checking whether the client requested the public key
  if (auth_rsa_private_key_.is_empty()) {
    logger_->log(binsrv::log_severity::warning,
                 "net    : cannot perform full authentication as "
                 "<replication_source.auth_rsa_private_key> is not configured");
    co_return false;
  }

  if (context.check_client_requested_public_key()) {
    co_await perform_public_key_dispatch(socket, context);
  }

  co_return context.check_client_full_authentication(auth_rsa_private_key_);
}

[[nodiscard]] boost::asio::awaitable<bool>
network_service::perform_connection_packet_exchange(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    connection_context &context) {
  network_buffer_type data;
  data.reserve(network_service::expected_packet_size);

  const auto remote_endpoint_str{extract_remote_endpoint_str(socket)};

  const std::chrono::seconds read_timeout{get_config().get<"read_timeout">()};
  const std::chrono::seconds write_timeout{get_config().get<"write_timeout">()};

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
  print_server_greeting(remote_endpoint_str, context);
  co_await async_write_mysql_frame(socket, server_greeting, write_timeout);
  logger_->log_format(binsrv::log_severity::debug,
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

  co_await async_read_mysql_frame(socket, data, read_timeout);
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : received client greeting ({} bytes from {})",
                      std::size(data), remote_endpoint_str);
  context.parse_client_greeting(data);
  print_client_greeting(remote_endpoint_str, context);

  if (!context.check_shared_plugin_auth_supported()) {
    logger_->log(binsrv::log_severity::warning,
                 "net    : client does not support plugin authentication");
    const auto access_denied{context.generate_encoded_access_denied()};
    print_error(remote_endpoint_str, context, "plugin auth required");
    co_await async_write_mysql_frame(socket, access_denied, write_timeout);
    logger_->log_format(binsrv::log_severity::debug,
                        "net    : sent server access denied ({} bytes to {})",
                        std::size(access_denied), remote_endpoint_str);
    co_return false;
  }

  if (context.get_client_auth_method() != context.get_server_auth_method()) {
    logger_->log_format(binsrv::log_severity::info,
                        "net    : client requested {} authentication that does "
                        "not match the one "
                        "associated with the user account ({})",
                        context.get_client_auth_method(),
                        context.get_server_auth_method());

    const auto auth_method_switch{
        context.generate_encoded_auth_method_switch()};
    print_generic(remote_endpoint_str, context, "auth method switch");
    co_await async_write_mysql_frame(socket, auth_method_switch, write_timeout);
    logger_->log_format(
        binsrv::log_severity::debug,
        "net    : sent server auth method switch ({} bytes to {})",
        std::size(auth_method_switch), remote_endpoint_str);

    co_await async_read_mysql_frame(socket, data, read_timeout);
    logger_->log_format(binsrv::log_severity::debug,
                        "net    : received client auth method switch response "
                        "({} bytes from {})",
                        std::size(data), remote_endpoint_str);
    context.parse_client_auth_method_switch(data);
    print_client_auth_method_switch(remote_endpoint_str, context);
  }
  bool authenticated{false};
  if (check_if_in_auth_cache(context)) {
    authenticated = co_await perform_fast_authentication(socket, context);
  } else {
    authenticated = co_await perform_full_authentication(socket, context);
  }

  if (!authenticated) {
    logger_->log_format(binsrv::log_severity::warning,
                        "net    : client authentication failed for {}",
                        context.get_client_username());
    const auto access_denied{context.generate_encoded_access_denied()};
    print_error(remote_endpoint_str, context, "auth failure");
    co_await async_write_mysql_frame(socket, access_denied, write_timeout);
    logger_->log_format(binsrv::log_severity::debug,
                        "net    : sent server access denied ({} bytes to {})",
                        std::size(access_denied), remote_endpoint_str);
    co_return false;
  }

  logger_->log_format(binsrv::log_severity::info,
                      "net    : client authentication succeeded for {}",
                      context.get_client_username());

  // sending server ok after successful authentication
  const auto auth_ok{context.generate_encoded_ok()};
  print_generic(remote_endpoint_str, context, "ok (auth)");
  co_await async_write_mysql_frame(socket, auth_ok, write_timeout);
  logger_->log_format(
      binsrv::log_severity::debug,
      "net    : sent server ok after authentication ({} bytes to {})",
      std::size(auth_ok), remote_endpoint_str);
  co_return true;
}

// MySQL session handling coroutine - writes server greeting, then receives and
// parses client greeting
[[nodiscard]] boost::asio::awaitable<void>
network_service::session(boost::asio::ip::tcp::socket socket) {
  const auto remote_endpoint_str{extract_remote_endpoint_str(socket)};

  const scope_tracer tracer(*logger_, "session " + remote_endpoint_str);

  const std::chrono::seconds read_timeout{get_config().get<"read_timeout">()};
  const std::chrono::seconds write_timeout{get_config().get<"write_timeout">()};

  try {
    network_buffer_type data;
    data.reserve(network_service::expected_packet_size);

    connection_context context{
        get_config().get<"authentication">().get<"user">(),
        get_config().get<"authentication">().get<"password">()};

    if (!co_await perform_connection_packet_exchange(socket, context)) {
      co_return;
    }

    // defining known queries container
    using query_handler_type =
        std::function<network_buffer_container(connection_context & context)>;
    using query_container = std::unordered_map<std::string, query_handler_type>;
    using prefix_query_container =
        std::vector<std::pair<std::string, query_handler_type>>;

    bool session_source_binlog_checksum{false};
    // Tracks the string value of @source_binlog_checksum /
    // @master_binlog_checksum for subsequent SELECT reads. std::nullopt means
    // the variable has never been assigned on this session (NULL on SELECT).
    util::optional_string session_source_binlog_checksum_name{};

    const auto set_checksum_on_query_handler =
        [&session_source_binlog_checksum,
         &session_source_binlog_checksum_name](connection_context &ctx) {
          session_source_binlog_checksum = true;
          session_source_binlog_checksum_name = "CRC32";
          network_buffer_container resultset;
          resultset.emplace_back(ctx.generate_encoded_ok());
          return resultset;
        };
    const auto set_checksum_off_query_handler =
        [&session_source_binlog_checksum,
         &session_source_binlog_checksum_name](connection_context &ctx) {
          session_source_binlog_checksum = false;
          session_source_binlog_checksum_name = "NONE";
          network_buffer_container resultset;
          resultset.emplace_back(ctx.generate_encoded_ok());
          return resultset;
        };

    // Replies OK without altering any session state. Suitable for parameterized
    // SET statements that we do not need to track (e.g. heartbeat period, NAMES
    // charset) - the replica only cares that the SET succeeds.
    //
    // Each handshake statement currently wired to this handler is here only
    // because PBS does not yet implement the feature that would consume its
    // payload. When PBS gains heartbeat emission on an idle downstream
    // connection, SET @{source,master}_heartbeat_period must switch to a
    // dedicated handler that records the per-session period and arms a
    // timer. When PBS gains a SHOW REPLICAS / connected-replicas view (or
    // uses the replica uuid for any other bookkeeping), SET @slave_uuid /
    // SET @replica_uuid must switch to a handler that parses the value and
    // stores it on the session. See the per-entry notes in
    // 'known_prefix_queries' below.
    const auto noop_ok_handler = [](connection_context &ctx) {
      network_buffer_container resultset;
      resultset.emplace_back(ctx.generate_encoded_ok());
      return resultset;
    };
    query_container known_queries{
        {"select * from tbl",
         [](connection_context &ctx) {
           using row_type =
               std::tuple<std::uint64_t, std::optional<std::uint64_t>,
                          std::string, std::optional<std::string>>;
           using row_collection_type = std::vector<row_type>;
           const row_collection_type rows{{1, 100, "Alice", "Cooper"},
                                          {2, {}, "Bob", {}}};
           const std::array column_names{
               column_name_pair{"id", "id"},
               column_name_pair{"optional_id", "optional_id"},
               column_name_pair{"name", "name"},
               column_name_pair{"optional_name", "optional_name"}};
           return ctx.encode_resultset(rows, column_names);
         }},
        {"select @@version_comment limit 1",
         [](connection_context &ctx) {
           using version_comment_record = std::tuple<std::string>;
           using version_comment_record_collection =
               std::vector<version_comment_record>;
           const version_comment_record_collection records{
               {"Percona Binlog Server - GPL"}};
           const std::array column_names{
               column_name_pair{"@@version_comment", ""}};
           return ctx.encode_resultset(records, column_names);
         }},
        {"SELECT VERSION()",
         [](connection_context &ctx) {
           using version_record = std::tuple<std::string>;
           using version_record_collection = std::vector<version_record>;
           const version_record_collection records{{"9.7.0"}};
           const std::array column_names{column_name_pair{"VERSION()", ""}};
           return ctx.encode_resultset(records, column_names);
         }},
        {"SET @source_binlog_checksum = 'NONE', @master_binlog_checksum = "
         "'NONE'",
         set_checksum_off_query_handler},
        {"SET @master_binlog_checksum = 'NONE', @source_binlog_checksum = "
         "'NONE'",
         set_checksum_off_query_handler},
        {"SET @source_binlog_checksum = 'CRC32', @master_binlog_checksum = "
         "'CRC32'",
         set_checksum_on_query_handler},
        {"SET @master_binlog_checksum = 'CRC32', @source_binlog_checksum = "
         "'CRC32'",
         set_checksum_on_query_handler},
        // Replica form: 'take your global binlog_checksum into my session
        // variable'. PBS always emits CRC32-checksummed events, so we treat
        // these as 'checksum on'. Both argument orderings are accepted.
        {"SET @master_binlog_checksum = @@global.binlog_checksum, "
         "@source_binlog_checksum = @@global.binlog_checksum",
         set_checksum_on_query_handler},
        {"SET @source_binlog_checksum = @@global.binlog_checksum, "
         "@master_binlog_checksum = @@global.binlog_checksum",
         set_checksum_on_query_handler},
        // Replica handshake: clock-skew probe. Returning server-side
        // 'now' is sufficient; the replica only logs a warning if the clocks
        // differ by too much.
        {"SELECT UNIX_TIMESTAMP()",
         [](connection_context &ctx) {
           // Signed to match MySQL/PS: Item_func_unix_timestamp::resolve_type
           // calls set_data_type_longlong() and never touches unsigned_flag,
           // so the column ships as BIGINT SIGNED.
           using row_type = std::tuple<std::int64_t>;
           const auto now_seconds{
               std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count()};
           const std::vector<row_type> rows{
               {static_cast<std::int64_t>(now_seconds)}};
           const std::array column_names{
               column_name_pair{"UNIX_TIMESTAMP()", ""}};
           return ctx.encode_resultset(rows, column_names);
         }},
        // Replica handshake: server_id advertised to downstream replicas. The
        // replica aborts its I/O thread when this collides with its own
        // server_id (loop detection). Taken from
        // replication_source.server_id in main_config - independent from
        // the upstream replication.server_id so operators can present
        // different identities on the two sides of PBS.
        {"SELECT @@GLOBAL.SERVER_ID",
         [&server_id =
              get_config().get<"server_id">()](connection_context &ctx) {
           using row_type = std::tuple<std::uint64_t>;
           const std::vector<row_type> rows{{server_id}};
           const std::array column_names{
               column_name_pair{"@@GLOBAL.SERVER_ID", ""}};
           return ctx.encode_resultset(rows, column_names);
         }},
        // Replica handshake: server_uuid advertised to downstream replicas.
        // Same anti-loop reasoning as SERVER_ID; must not collide with the
        // replica's own UUID. Taken from replication_source.server_uuid in
        // main_config (canonical 8-4-4-4-12 hex form; validated at load).
        {"SELECT @@GLOBAL.SERVER_UUID",
         [&server_uuid =
              get_config().get<"server_uuid">()](connection_context &ctx) {
           using row_type = std::tuple<std::string>;
           const std::vector<row_type> rows{{server_uuid}};
           const std::array column_names{
               column_name_pair{"@@GLOBAL.SERVER_UUID", ""}};
           return ctx.encode_resultset(rows, column_names);
         }},
        // Replica handshake: both variants of 'read back the checksum
        // algorithm agreed with the source'. Returns whatever the session
        // SET @...binlog_checksum left behind, or NULL if never set.
        {"SELECT @source_binlog_checksum",
         [&session_source_binlog_checksum_name](connection_context &ctx) {
           using row_type = std::tuple<std::optional<std::string>>;
           const std::vector<row_type> rows{
               {session_source_binlog_checksum_name}};
           const std::array column_names{
               column_name_pair{"@source_binlog_checksum", ""}};
           return ctx.encode_resultset(rows, column_names);
         }},
        {"SELECT @master_binlog_checksum",
         [&session_source_binlog_checksum_name](connection_context &ctx) {
           using row_type = std::tuple<std::optional<std::string>>;
           const std::vector<row_type> rows{
               {session_source_binlog_checksum_name}};
           const std::array column_names{
               column_name_pair{"@master_binlog_checksum", ""}};
           return ctx.encode_resultset(rows, column_names);
         }},
        // Replica handshake: GTID mode advertised to downstream replicas.
        // Derived from the actual mode the storage is operating in (which
        // is in turn determined by how the binlog files it holds were
        // captured), not from the upstream config - a position-mode storage
        // must advertise OFF regardless of how upstream pull is configured.
        {"SELECT @@GLOBAL.GTID_MODE",
         [mode = storage_->get_replication_mode()](connection_context &ctx) {
           using row_type = std::tuple<std::string>;
           const bool gtid_mode_on{mode == binsrv::replication_mode_type::gtid};
           const std::vector<row_type> rows{
               {gtid_mode_on ? std::string{"ON"} : std::string{"OFF"}}};
           const std::array column_names{
               column_name_pair{"@@GLOBAL.GTID_MODE", ""}};
           return ctx.encode_resultset(rows, column_names);
         }}};

    // Statements whose payload varies between invocations (e.g. the exact
    // heartbeat-period nanosecond count) are matched by prefix instead of by
    // exact string. First match wins, so order the entries from longest to
    // shortest prefix if they could ever overlap.
    const prefix_query_container known_prefix_queries{
        // SET @{source,master}_heartbeat_period = <nanoseconds>. The replica
        // asks the source to emit a Heartbeat_log_event every <N> nanoseconds
        // of binlog idleness so the receiver thread can detect a stalled
        // source without waiting for connection.read_timeout. PBS does not
        // emit heartbeats today, so the setting is accepted and discarded.
        // TODO: when heartbeat emission is implemented, replace noop_ok_handler
        // with a dedicated handler that parses the nanosecond count, stores
        // it per-session, and (re)arms the heartbeat timer on the current
        // COM_BINLOG_DUMP stream.
        {"SET @source_heartbeat_period", noop_ok_handler},
        {"SET @master_heartbeat_period", noop_ok_handler},
        // SET NAMES <charset> - sent by libmysqlclient when the replica opens
        // the connection to initialize the session charset. PBS only speaks
        // the binary binlog event stream, so the session charset has no
        // observable effect and the SET is a no-op by design.
        {"SET NAMES ", noop_ok_handler},
        // SET @slave_uuid = '<replica uuid>'[, @replica_uuid = ...]. The
        // replica tells the source its own uuid for anti-loop reporting and
        // for the SHOW REPLICAS view. PBS exposes neither today, so the
        // value is accepted and discarded.
        // TODO: when PBS gains SHOW REPLICAS / a connected-replicas view
        // (or needs the uuid for duplicate-replica detection), replace
        // noop_ok_handler with a dedicated handler that parses the uuid and
        // stores it on the session.
        {"SET @slave_uuid", noop_ok_handler},
        {"SET @replica_uuid", noop_ok_handler},
    };

    // starting command loop
    bool terminated{false};
    while (!terminated) {
      context.enter_command_loop_iteration();
      co_await async_read_mysql_frame(socket, data, read_timeout);
      logger_->log_format(binsrv::log_severity::debug,
                          "net    : received client command ({} bytes from {})",
                          std::size(data), remote_endpoint_str);
      context.parse_client_command(data);
      print_client_command(remote_endpoint_str, context);

      switch (context.get_client_mysql_command()) {
      case minimysql::client_command_type::query: {
        const std::string &statement{context.get_client_statement()};
        const query_handler_type *selected_handler{nullptr};
        const auto exact_match_it{known_queries.find(statement)};
        if (exact_match_it != std::end(known_queries)) {
          selected_handler = &exact_match_it->second;
        } else {
          const auto prefix_match_it{std::ranges::find_if(
              known_prefix_queries, [&statement](const auto &entry) {
                return std::string_view{statement}.starts_with(entry.first);
              })};
          if (prefix_match_it != std::end(known_prefix_queries)) {
            selected_handler = &prefix_match_it->second;
          }
        }
        if (selected_handler != nullptr) {
          const auto resultset{(*selected_handler)(context)};
          print_generic(remote_endpoint_str, context, "resultset");
          co_await minimysql::async_write_mysql_frames(socket, resultset,
                                                       write_timeout);
          logger_->log_format(
              binsrv::log_severity::debug,
              "net    : sent server resultset ({} frames to {})",
              std::size(resultset), remote_endpoint_str);
        } else {
          // return 'syntax error' for every other query
          const auto syntax_error = context.generate_encoded_syntax_error();
          print_error(remote_endpoint_str, context, "syntax error");
          co_await async_write_mysql_frame(socket, syntax_error, write_timeout);
          logger_->log_format(
              binsrv::log_severity::debug,
              "net    : sent server syntax error ({} bytes to {})",
              std::size(syntax_error), remote_endpoint_str);
        }
      } break;
      case client_command_type::ping: {
        const auto ok_after_ping{context.generate_encoded_ok()};
        print_generic(remote_endpoint_str, context, "ok (ping success)");
        co_await async_write_mysql_frame(socket, ok_after_ping, write_timeout);
        logger_->log_format(
            binsrv::log_severity::debug,
            "net    : sent server ok after ping ({} bytes to {})",
            std::size(ok_after_ping), remote_endpoint_str);
      } break;
      case minimysql::client_command_type::register_replica: {
        // Replica identifies itself just before COM_BINLOG_DUMP. The payload
        // (server_id, hostname, port, ...) is useful mainly for SHOW REPLICAS
        // and for detecting multiple replicas sharing the same server_id;
        // PBS exposes neither view today, so the frame is parsed only far
        // enough to advance the sequence number and the command is answered
        // with a bare OK.
        // TODO: when PBS gains a connected-replicas view (SHOW REPLICAS /
        // same-server_id duplicate detection), replace this bare-OK branch
        // with a handler that extracts server_id/hostname/port/user from
        // the frame payload, stores them on the session, and registers the
        // session in a process-wide table.
        const auto ok_after_register_replica{context.generate_encoded_ok()};
        print_generic(remote_endpoint_str, context, "ok (register replica)");
        co_await minimysql::async_write_mysql_frame(
            socket, ok_after_register_replica, write_timeout);
        logger_->log_format(
            binsrv::log_severity::debug,
            "net    : sent server ok after register replica ({} bytes to {})",
            std::size(ok_after_register_replica), remote_endpoint_str);
      } break;
      case minimysql::client_command_type::binlog_dump: {
        // Refuse to dump if the replica never set @source_binlog_checksum.
        // PBS may send events with a CRC32 trailer, and a client that is
        // not checksum-aware cannot parse them.
        if (!session_source_binlog_checksum_name.has_value()) {
          static constexpr std::uint16_t er_source_fatal_error_reading_binlog{
              1236U};
          const auto rejection_packet{context.generate_encoded_error(
              er_source_fatal_error_reading_binlog,
              "Replica did not declare @source_binlog_checksum before "
              "COM_BINLOG_DUMP; PBS refuses to serve replicas that are not "
              "checksum-aware.",
              "HY000")};
          print_error(remote_endpoint_str, context,
                      "missing @source_binlog_checksum declaration");
          co_await minimysql::async_write_mysql_frame(socket, rejection_packet,
                                                      write_timeout);
          logger_->log_format(binsrv::log_severity::debug,
                              "net    : sent server error (missing "
                              "@source_binlog_checksum) ({} bytes to {})",
                              std::size(rejection_packet), remote_endpoint_str);
          terminated = true;
          break;
        }
        co_await handle_binlog_dump_command(socket, context,
                                            session_source_binlog_checksum);
        terminated = true;
      } break;
      case client_command_type::quit: {
        // TODO: read EOF from the socket to make sure the client has closed the
        // connection instead of just closing it from our side
        terminated = true;
      } break;
      default: {
        const auto unknown_command_error =
            context.generate_encoded_unknown_command();
        print_error(remote_endpoint_str, context, "unknown command");
        co_await async_write_mysql_frame(socket, unknown_command_error,
                                         write_timeout);
        logger_->log_format(
            binsrv::log_severity::debug,
            "net    : sent server unknown command ({} bytes to {})",
            std::size(unknown_command_error), remote_endpoint_str);
      }
      }
    }
  } catch (...) {
    handle_exception("session " + remote_endpoint_str);
  }
}
#pragma GCC diagnostic pop

// listener coroutine - accepts incoming connections and spawns a session
// coroutine for each accepted connection
boost::asio::awaitable<void> network_service::listener() {
  const scope_tracer tracer(*logger_, "listener");

  auto executor = acceptor_->get_executor();

  try {
    for (;;) {
      boost::system::error_code listener_ec;
      boost::asio::ip::tcp::socket socket = co_await acceptor_->async_accept(
          boost::asio::redirect_error(boost::asio::use_awaitable, listener_ec));
      if (listener_ec) {
        if (listener_ec != boost::asio::error::operation_aborted &&
            listener_ec != boost::asio::error::bad_descriptor) {
          throw boost::system::system_error{listener_ec};
        }
        logger_->log(binsrv::log_severity::info, "net    : listener stopped");
        break;
      }

      const auto remote_endpoint_str{extract_remote_endpoint_str(socket)};
      logger_->log_format(binsrv::log_severity::info,
                          "net    : accepted connection from {}",
                          remote_endpoint_str);

      // NOLINTNEXTLINE(misc-include-cleaner)
      boost::asio::co_spawn(executor, session(std::move(socket)),
                            boost::asio::detached);
    }
  } catch (...) {
    handle_exception("listener");
  }
}

network_service::network_service(binsrv::basic_logger_ptr logger,
                                 boost::asio::io_context &io_ctx,
                                 binsrv::storage_ptr storage,
                                 const binsrv::replication_source_config &cfg)
    : logger_{std::move(logger)}, io_ctx_{&io_ctx},
      storage_{std::move(storage)}, config_{&cfg}, auth_rsa_private_key_{},
      auth_rsa_public_key_pem_{},
      acceptor_{std::make_unique<acceptor_type>(
          io_ctx, boost::asio::ip::tcp::endpoint{boost::asio::ip::tcp::v4(),
                                                 cfg.get<"port">()})} {
  assert(logger_);

  load_auth_rsa_private_key();

  // NOLINTNEXTLINE(misc-include-cleaner)
  boost::asio::co_spawn(get_io_ctx(), listener(), boost::asio::detached);
}

network_service::~network_service() = default;

void network_service::load_auth_rsa_private_key() {
  const auto &optional_key_path{get_config().get<"auth_rsa_private_key">()};
  if (!optional_key_path.has_value()) {
    logger_->log(
        binsrv::log_severity::info,
        "net    : no authentication RSA private key configured, full "
        "authentication for non-SSL/TLS connections will not be available");
    return;
  }

  static constexpr std::size_t max_pem_file_size{65536UZ};
  const auto pem_content{
      util::read_file_content("authentication RSA private key file",
                              *optional_key_path, max_pem_file_size)};
  auth_rsa_private_key_ = opensslpp::evp_pkey::import_private_pem(
      util::as_string_view(pem_content));
  logger_->log_format(binsrv::log_severity::info,
                      "net    : loaded {}-bit authentication RSA private key "
                      "from {}",
                      auth_rsa_private_key_.get_size_in_bits(),
                      *optional_key_path);

  // the public component is extracted from the private key only once and
  // then sent (in PEM format) to every client that requests it during
  // full authentication (e.g. 'mysql --get-server-public-key')
  auth_rsa_public_key_pem_ = opensslpp::evp_pkey::export_public_pem(
      auth_rsa_private_key_.derive_public_key());
}

[[nodiscard]] std::string network_service::extract_remote_endpoint_str(
    const boost::asio::ip::tcp::socket &socket) {
  boost::system::error_code socket_ec;
  const auto remote_endpoint{socket.remote_endpoint(socket_ec)};
  return boost::lexical_cast<std::string>(remote_endpoint);
}

} // namespace minimysql
