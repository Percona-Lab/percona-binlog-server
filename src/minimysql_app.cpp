// Copyright (c) 2023-20264 Percona and/or its affiliates.
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

#include <array>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/lexical_cast.hpp>

#include <boost/algorithm/hex.hpp>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
// Include What You Use pragma is needed here because the 'co_spawn()'
// function that is used in this file is located in the 'impl' subdirectory
// of the 'asio' headers ('boost/asio/impl/co_spawn.hpp') and should not be
// included directly, but the 'boost/asio/co_spawn.hpp' header is a public
// one that includes the 'impl' header
#include <boost/asio/co_spawn.hpp> // IWYU pragma: keep
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/describe/enum_to_string.hpp>

#include <boost/system/system_error.hpp>

#include "minimysql/connection_context.hpp"

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

class hardcoded_event_data {
public:
  using event_data_type = std::string;
  static constexpr auto number_of_predefined_events{10UZ};
  using event_data_container =
      std::array<event_data_type, number_of_predefined_events>;

  hardcoded_event_data()
      : events_{hex_to_bin(hardcoded_event_data::rotate_artificial),
                hex_to_bin(hardcoded_event_data::fde),
                hex_to_bin(hardcoded_event_data::previous_gtids),
                hex_to_bin(hardcoded_event_data::first_gtid_log),
                hex_to_bin(hardcoded_event_data::first_query),
                hex_to_bin(hardcoded_event_data::second_gtid_log),
                hex_to_bin(hardcoded_event_data::second_query),
                hex_to_bin(hardcoded_event_data::second_table_map),
                hex_to_bin(hardcoded_event_data::second_write_rows),
                hex_to_bin(hardcoded_event_data::second_xid)} {}

  [[nodiscard]] const event_data_container &get_events() const noexcept {
    return events_;
  }

private:
  static constexpr std::string_view rotate_artificial{
      "00 00 00 00 04 01 00 00 00 28 00 00 00 00 00 00"
      "00 20 00 04 00 00 00 00 00 00 00 62 69 6e 6c 6f"
      "67 2e 30 30 30 30 30 31                        "};
  static constexpr std::string_view fde{
      "1a b2 1c 6a 0f 01 00 00 00 7b 00 00 00 7f 00 00"
      "00 00 00 04 00 38 2e 34 2e 37 2d 37 00 00 00 00"
      "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
      "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
      "00 00 00 00 00 00 00 1a b2 1c 6a 13 00 0d 00 08"
      "00 00 00 00 04 00 04 00 00 00 63 00 04 1a 08 00"
      "00 00 00 00 00 02 00 00 00 0a 0a 0a 2a 2a 00 12"
      "34 00 0a 28 00 00 01 ee c6 67 82               "};
  static constexpr std::string_view previous_gtids{
      "1a b2 1c 6a 23 01 00 00 00 1f 00 00 00 9e 00 00"
      "00 80 00 00 00 00 00 00 00 00 00 09 5d aa 16   "};
  static constexpr std::string_view first_gtid_log{
      "52 b2 1c 6a 21 01 00 00 00 4d 00 00 00 eb 00 00"
      "00 00 00 01 b1 14 55 0c 5d 3d 11 f1 a7 3a 00 15"
      "5d f1 f4 92 01 00 00 00 00 00 00 00 02 00 00 00"
      "00 00 00 00 00 01 00 00 00 00 00 00 00 9c 7e f6"
      "5f 24 53 06 cb 17 3a 01 00 80 dc f9 5c         "};
  static constexpr std::string_view first_query{
      "52 b2 1c 6a 02 01 00 00 00 7e 00 00 00 69 01 00"
      "00 00 00 09 00 00 00 00 00 00 00 04 00 00 2f 00"
      "00 00 00 00 00 01 20 00 a0 45 00 00 00 00 06 03"
      "73 74 64 04 ff 00 ff 00 ff 00 0c 01 74 65 73 74"
      "00 11 06 00 00 00 00 00 00 00 12 ff 00 13 00 74"
      "65 73 74 00 43 52 45 41 54 45 20 54 41 42 4c 45"
      "20 74 31 28 69 64 20 53 45 52 49 41 4c 20 50 52"
      "49 4d 41 52 59 20 4b 45 59 29 d9 1d 2b 2b      "};
  static constexpr std::string_view second_gtid_log{
      "57 b2 1c 6a 21 01 00 00 00 4f 00 00 00 b8 01 00"
      "00 00 00 00 b1 14 55 0c 5d 3d 11 f1 a7 3a 00 15"
      "5d f1 f4 92 02 00 00 00 00 00 00 00 02 01 00 00"
      "00 00 00 00 00 02 00 00 00 00 00 00 00 8a 35 4f"
      "60 24 53 06 fc 15 01 17 3a 01 00 24 35 52 71   "};
  static constexpr std::string_view second_query{
      "57 b2 1c 6a 02 01 00 00 00 4b 00 00 00 03 02 00"
      "00 08 00 09 00 00 00 00 00 00 00 04 00 00 1d 00"
      "00 00 00 00 00 01 20 00 a0 45 00 00 00 00 06 03"
      "73 74 64 04 ff 00 ff 00 ff 00 12 ff 00 74 65 73"
      "74 00 42 45 47 49 4e c8 4c ce 80               "};
  static constexpr std::string_view second_table_map{
      "57 b2 1c 6a 13 01 00 00 00 30 00 00 00 33 02 00"
      "00 00 00 73 00 00 00 00 00 01 00 04 74 65 73 74"
      "00 02 74 31 00 01 08 00 00 01 01 80 21 d6 bc 8c"};
  static constexpr std::string_view second_write_rows{
      "57 b2 1c 6a 1e 01 00 00 00 2c 00 00 00 5f 02 00"
      "00 00 00 73 00 00 00 00 00 01 00 02 00 01 ff 00"
      "01 00 00 00 00 00 00 00 82 65 a7 86            "};
  static constexpr std::string_view second_xid{
      "57 b2 1c 6a 10 01 00 00 00 1f 00 00 00 7e 02 00"
      "00 00 00 07 00 00 00 00 00 00 00 e3 36 06 8f   "};

  static std::string hex_to_bin(std::string_view hex) {
    std::string filtered_hex{hex};
    std::erase(filtered_hex, ' ');
    return boost::algorithm::unhex(filtered_hex);
  }

  event_data_container events_;
};

namespace {

constexpr std::uint16_t listening_port{3307};
constexpr std::size_t expected_packet_size{4096};
constexpr std::chrono::seconds session_authentication_timeout{10};
constexpr std::chrono::seconds session_command_timeout{120};
constexpr std::size_t max_payload_size{
    16UZ * 1024UZ * 1024UZ}; // 16 MiB - MySQL's maximum packet size

constexpr std::string_view default_username{"rpl"};
constexpr std::string_view default_password{"password"};

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

// a helper coroutine for reading MySQL frame with a timeout - returns a tuple
// of (error_code, bytes_transferred)

// as this coroutine is always used with co_await, it is absolutely safe to
// pass arguments by reference here
boost::asio::awaitable<void> async_read_mysql_frame(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    minimysql::connection_buffer_type &payload,
    std::chrono::steady_clock::duration timeout) {
  using namespace boost::asio::experimental::awaitable_operators;

  minimysql::connection_buffer_type local_payload{};
  auto payload_buffer{boost::asio::dynamic_buffer(local_payload)};

  boost::asio::steady_timer read_timer{socket.get_executor(), timeout};
  // timed_read_result is a variant of 2 results (one form async read, one from
  // timer)
  auto timed_read_result{co_await (
      boost::asio::async_read(
          socket, payload_buffer,
          boost::asio::transfer_exactly(
              minimysql::connection_context::get_frame_header_length()),
          boost::asio::as_tuple(boost::asio::use_awaitable)) ||
      read_timer.async_wait(
          boost::asio::as_tuple(boost::asio::use_awaitable)))};

  // if timer finished first, we consider it a timeout error
  if (timed_read_result.index() != 0UZ) {
    throw boost::system::system_error{boost::asio::error::timed_out,
                                      "frame header read timeout"};
  }

  // extracting the result of async_read for header
  const auto &header_read_result{std::get<0UZ>(timed_read_result)};

  // extracting the error code from the header_read_result and throwing if there
  // was an error
  const auto header_read_error_code{std::get<0UZ>(header_read_result)};
  if (header_read_error_code) {
    throw boost::system::system_error{header_read_error_code,
                                      "frame header read error"};
  }

  assert(std::size(local_payload) ==
         minimysql::connection_context::get_frame_header_length());
  assert(std::get<1UZ>(header_read_result) ==
         minimysql::connection_context::get_frame_header_length());

  // checking the payload size from the header and throwing if it is larger than
  // our maximum allowed size
  auto payload_size{
      minimysql::connection_context::parse_frame_header(local_payload)};
  if (payload_size == max_payload_size) {
    throw boost::system::system_error{boost::asio::error::message_size,
                                      "frame payload size too large"};
  }

  // it is ok to reuse the same timer for reading the payload - calling
  // expires_after() cancels any previously set timeout
  read_timer.expires_after(timeout);
  // reusing timed_read result for reading the payload
  timed_read_result = co_await (
      boost::asio::async_read(
          socket, payload_buffer, boost::asio::transfer_exactly(payload_size),
          boost::asio::as_tuple(boost::asio::use_awaitable)) ||
      read_timer.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable)));

  // if timer finished first, we consider it a timeout error
  if (timed_read_result.index() != 0UZ) {
    throw boost::system::system_error{boost::asio::error::timed_out,
                                      "frame payload read timeout"};
  }

  // extracting the result of async_read for payload
  const auto &payload_read_result{std::get<0UZ>(timed_read_result)};
  // extracting the error code from payload_read_result and throwing if there
  // was an error
  const auto payload_read_error_code{std::get<0UZ>(payload_read_result)};
  if (payload_read_error_code) {
    throw boost::system::system_error{payload_read_error_code,
                                      "frame payload read error"};
  }
  assert(std::size(local_payload) ==
         minimysql::connection_context::get_frame_header_length() +
             payload_size);
  assert(std::get<1UZ>(payload_read_result) == payload_size);

  payload.swap(local_payload);
}

// a helper coroutine for writing with a timeout -
// throws on error

// as this coroutine is always used with co_await, it is absolutely safe to
// pass arguments by reference here
boost::asio::awaitable<void> async_write_mysql_frame(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const minimysql::connection_buffer_type &payload,
    std::chrono::steady_clock::duration timeout) {
  using namespace boost::asio::experimental::awaitable_operators;

  boost::asio::steady_timer write_timer{socket.get_executor(), timeout};
  // timed_write_result is a variant of 2 results (one form async write, one
  // from timer)
  auto timed_write_result{
      co_await (boost::asio::async_write(
                    socket, boost::asio::buffer(payload),
                    boost::asio::as_tuple(boost::asio::use_awaitable)) ||
                write_timer.async_wait(
                    boost::asio::as_tuple(boost::asio::use_awaitable)))};

  // if timer finished first, we consider it a timeout error
  if (timed_write_result.index() != 0UZ) {
    throw boost::system::system_error{boost::asio::error::timed_out,
                                      "frame write timeout"};
  }

  // extracting the result of async_write
  const auto &write_result{std::get<0UZ>(timed_write_result)};
  // extracting the error code from async_write result and throwing if there was
  // an error
  const auto write_error_code{std::get<0UZ>(write_result)};
  if (write_error_code) {
    throw boost::system::system_error{write_error_code, "frame write error"};
  }
  assert(std::get<1UZ>(write_result) == std::size(payload));
}

// a helper coroutine for writing a collection of frames with a timeout -
// throws on error

// as this coroutine is always used with co_await, it is absolutely safe to
// pass arguments by reference here
boost::asio::awaitable<void> async_write_mysql_frames(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const minimysql::connection_buffer_container &payloads,
    std::chrono::steady_clock::duration timeout) {
  for (const auto &payload : payloads) {
    co_await async_write_mysql_frame(socket, payload, timeout);
  }
}

// MySQL session handling coroutine - writes server greeting, then receives and
// parses client greeting
boost::asio::awaitable<void> session(boost::asio::ip::tcp::socket socket) {
  boost::system::error_code session_ec;
  const auto remote_endpoint{socket.remote_endpoint(session_ec)};

  const scope_tracer tracer("session " +
                            boost::lexical_cast<std::string>(remote_endpoint));

  try {
    minimysql::connection_buffer_type data;
    data.reserve(expected_packet_size);

    minimysql::connection_context context{default_username, default_password};

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
    co_await async_write_mysql_frame(socket, server_greeting,
                                     session_authentication_timeout);
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

    co_await async_read_mysql_frame(socket, data,
                                    session_authentication_timeout);
    std::cout << "received client greeting (" << std::size(data)
              << " bytes from " << remote_endpoint << ")\n";
    context.parse_client_greeting(data);
    print_client_greeting(remote_endpoint, context);

    if (!context.check_shared_plugin_auth_supported()) {
      std::cout << "client does not support plugin authentication\n";
      const auto access_denied{context.generate_encoded_access_denied()};
      print_error(remote_endpoint, context, "plugin auth required");
      co_await async_write_mysql_frame(socket, access_denied,
                                       session_authentication_timeout);
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
      co_await async_write_mysql_frame(socket, access_denied,
                                       session_authentication_timeout);
      std::cout << "sent server access denied (" << std::size(access_denied)
                << " bytes to " << remote_endpoint << ")\n";
      co_return;
    }

    std::cout << "client authentication succeeded for "
              << context.get_client_username() << '\n';

    // sending fast auth success
    const auto fast_auth_success{context.generate_encoded_fast_auth()};
    print_generic(remote_endpoint, context, "auth method data (fast auth)");
    co_await async_write_mysql_frame(socket, fast_auth_success,
                                     session_authentication_timeout);
    std::cout << "sent server fast auth success ("
              << std::size(fast_auth_success) << " bytes to " << remote_endpoint
              << ")\n";

    // sending server ok after successful authentication
    const auto auth_ok{context.generate_encoded_ok()};
    print_generic(remote_endpoint, context, "ok (auth)");
    co_await async_write_mysql_frame(socket, auth_ok,
                                     session_authentication_timeout);
    std::cout << "sent server ok after authentication (" << std::size(auth_ok)
              << " bytes to " << remote_endpoint << ")\n";

    // defining known queries container
    using query_handler_type =
        std::function<minimysql::connection_buffer_container(
            minimysql::connection_context & context)>;
    using query_container = std::unordered_map<std::string, query_handler_type>;

    const auto set_checksum_query_handler =
        [](minimysql::connection_context &ctx) {
          minimysql::connection_buffer_container resultset;
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
      co_await async_read_mysql_frame(socket, data, session_command_timeout);
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
          co_await async_write_mysql_frames(socket, resultset,
                                            session_command_timeout);
          std::cout << "sent server resultset (" << std::size(resultset)
                    << " frames to " << remote_endpoint << ")\n";
        } else {
          // return 'syntax error' for every other query
          const auto syntax_error = context.generate_encoded_syntax_error();
          print_error(remote_endpoint, context, "syntax error");
          co_await async_write_mysql_frame(socket, syntax_error,
                                           session_command_timeout);
          std::cout << "sent server syntax error (" << std::size(syntax_error)
                    << " bytes to " << remote_endpoint << ")\n";
        }
      } break;
      case minimysql::client_command_type::ping: {
        const auto ok_after_ping{context.generate_encoded_ok()};
        print_generic(remote_endpoint, context, "ok (ping success)");
        co_await async_write_mysql_frame(socket, ok_after_ping,
                                         session_command_timeout);
        std::cout << "sent server ok after ping (" << std::size(ok_after_ping)
                  << " bytes to " << remote_endpoint << ")\n";
      } break;
      case minimysql::client_command_type::binlog_dump: {
        const hardcoded_event_data event_block;
        for (const auto &event_data : event_block.get_events()) {
          const auto event{context.generate_encoded_binlog_event(event_data)};
          print_generic(remote_endpoint, context, "binlog event");
          co_await async_write_mysql_frame(socket, event,
                                           session_command_timeout);
          std::cout << "sent server binlog event (" << std::size(event)
                    << " bytes to " << remote_endpoint << ")\n";
          // co_await async_read_mysql_frame(socket, data,
          // session_command_timeout); std::cout << "received binlog event reply
          // command (" << std::size(data) << " bytes from " << remote_endpoint
          // << ")\n";
        }
        const auto eof = context.generate_encoded_eof();
        print_generic(remote_endpoint, context, "binlog eof");
        co_await async_write_mysql_frame(socket, eof, session_command_timeout);
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
        co_await async_write_mysql_frame(socket, unknown_command_error,
                                         session_command_timeout);
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

// listener coroutine - accepts incoming connections and spawns a session
// coroutine for each accepted connection

// it is safe to pass the acceptor by reference here, as the instance of the
// acceptor object is guaranteed to outlive the listener coroutine, as it is
// created in main() and destroyed after the io_context.run() call returns
boost::asio::awaitable<void>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
listener(boost::asio::ip::tcp::acceptor &acceptor) {
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
      boost::asio::co_spawn(executor, session(std::move(socket)),
                            boost::asio::detached);
    }
  } catch (...) {
    handle_exception("listener");
  }
}

} // anonymous namespace

int main(int /* argc */, char * /* argv */[]) {
  int res{EXIT_FAILURE};
  try {
    std::cout << "starting mini-mysql-server" << '\n';
    boost::asio::io_context ctx;
    const boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(),
                                                  listening_port);
    boost::asio::ip::tcp::acceptor acceptor(ctx, endpoint);

    boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
      std::cout << "received termination signal\n";
      // stop accepting new clients; existing sessions keep running
      boost::system::error_code close_ec;
      acceptor.close(close_ec);
    });

    // NOLINTNEXTLINE(misc-include-cleaner)
    boost::asio::co_spawn(ctx, listener(acceptor), boost::asio::detached);
    const auto ctx_run_result{ctx.run()};
    std::cout << "ctx.run() returned " << ctx_run_result << '\n';
    res = EXIT_SUCCESS;
    std::cout << "stopping mini-mysql-server\n";
  } catch (...) {
    handle_exception("main");
  }

  return res;
}
