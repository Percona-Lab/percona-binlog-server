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

#include "minimysql/session_frame_stream.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>

#include <boost/lexical_cast.hpp>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/read.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"

#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream_base.hpp>

#pragma GCC diagnostic pop

// 'boost::system::error_code' is defined in one of the internal 'detail'
// headers, which should not be included directly - so, we including
// the public header and instruct IWYU that we really need it
#include <boost/system/error_code.hpp> // IWYU pragma: keep
#include <boost/system/system_error.hpp>

#include "minimysql/connection_context_fwd.hpp"

#include "util/exception_location_helpers.hpp"

namespace minimysql {

class session_frame_stream::timer_guard {
public:
  timer_guard(boost::asio::steady_timer &timer,
              std::chrono::steady_clock::duration timeout)
      : timer_{&timer} {
    timer_->expires_after(timeout);
  }

  timer_guard(const timer_guard &) = delete;
  timer_guard(timer_guard &&) = delete;
  timer_guard &operator=(const timer_guard &) = delete;
  timer_guard &operator=(timer_guard &&) = delete;

  ~timer_guard() {
    // unfortunately timer cancel() does not have a error_code version,
    // so putting it into the try-catch block
    try {
      timer_->cancel();
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
  }

private:
  boost::asio::steady_timer *timer_;
};

session_frame_stream::session_frame_stream(
    boost::asio::basic_stream_socket<boost::asio::ip::tcp> socket,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::chrono::steady_clock::duration read_timeout,
    std::chrono::steady_clock::duration write_timeout)
    : stream_{std::move(socket)}, timer_{stream_.get_executor()},
      read_timeout_(read_timeout), write_timeout_(write_timeout) {
  // NOLINTNEXTLINE(misc-include-cleaner)
  boost::system::error_code socket_ec;
  const auto remote_endpoint{
      stream_.get_underlying_stream().remote_endpoint(socket_ec)};
  readable_remote_endpoint_ = boost::lexical_cast<std::string>(remote_endpoint);
}

boost::asio::awaitable<void> session_frame_stream::async_upgrade_to_secure(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ssl::context &context,
    boost::asio::ssl::stream_base::handshake_type role) {
  using namespace boost::asio::experimental::awaitable_operators;

  // as this is a handshake operation (send and receive), we sum up the read
  // and write timeouts here
  const timer_guard upgrade_guard{timer_, read_timeout_ + write_timeout_};
  // timed_upgrade_result is a variant of 2 results (one form async handshake,
  // one from timer)
  auto timed_upgrade_result{co_await (
      stream_.async_upgrade_to_secure(
          context, role, boost::asio::as_tuple(boost::asio::use_awaitable)) ||
      timer_.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable)))};

  if (timed_upgrade_result.index() != 0UZ) {
    util::exception_location().raise<boost::system::system_error>(
        boost::asio::error::timed_out, "upgrade to secure timeout");
  }

  const auto &upgrade_result{std::get<0UZ>(timed_upgrade_result)};
  const auto upgrade_error_code{std::get<0UZ>(upgrade_result)};
  if (upgrade_error_code) {
    util::exception_location().raise<boost::system::system_error>(
        upgrade_error_code, "upgrade to secure error");
  }
}

boost::asio::awaitable<void> session_frame_stream::async_secure_shutdown() {
  if (stream_.is_secure()) {
    using namespace boost::asio::experimental::awaitable_operators;

    // as this is a handshake operation (send and receive), we sum up the read
    // and write timeouts here
    const timer_guard shutdown_guard{timer_, read_timeout_ + write_timeout_};
    // timed_shutdown_result is a variant of 2 results (one form async shutdown,
    // one from timer)
    auto timed_shutdown_result{co_await (
        stream_.async_secure_shutdown(
            boost::asio::as_tuple(boost::asio::use_awaitable)) ||
        timer_.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable)))};

    if (timed_shutdown_result.index() != 0UZ) {
      util::exception_location().raise<boost::system::system_error>(
          boost::asio::error::timed_out, "secure shutdown timeout");
    }

    const auto &shutdown_result{std::get<0UZ>(timed_shutdown_result)};
    const auto shutdown_error_code{std::get<0UZ>(shutdown_result)};
    if (shutdown_error_code) {
      util::exception_location().raise<boost::system::system_error>(
          shutdown_error_code, "secure shutdown error");
    }
  }
}

// a helper coroutine for reading MySQL frame with a timeout -
// throws on error
boost::asio::awaitable<void> session_frame_stream::async_read_mysql_frame(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    network_buffer_type &payload) {
  using namespace boost::asio::experimental::awaitable_operators;

  network_buffer_type local_payload{};
  auto payload_buffer{boost::asio::dynamic_buffer(local_payload)};

  std::size_t payload_size{};
  {
    const timer_guard read_timer_guard{timer_, read_timeout_};
    // timed_read_result is a variant of 2 results (one form async read, one
    // from timer)
    auto timed_read_result{co_await (
        boost::asio::async_read(
            stream_, payload_buffer,
            boost::asio::transfer_exactly(get_frame_header_length()),
            boost::asio::as_tuple(boost::asio::use_awaitable)) ||
        timer_.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable)))};

    // if timer finished first, we consider it a timeout error
    if (timed_read_result.index() != 0UZ) {
      util::exception_location().raise<boost::system::system_error>(
          boost::asio::error::timed_out, "frame header read timeout");
    }

    // extracting the result of async_read for header
    const auto &header_read_result{std::get<0UZ>(timed_read_result)};

    // extracting the error code from the header_read_result and throwing if
    // there was an error
    const auto header_read_error_code{std::get<0UZ>(header_read_result)};
    if (header_read_error_code) {
      util::exception_location().raise<boost::system::system_error>(
          header_read_error_code, "frame header read error");
    }

    assert(std::size(local_payload) == get_frame_header_length());
    assert(std::get<1UZ>(header_read_result) == get_frame_header_length());

    // checking the payload size from the header and throwing if it is larger
    // than our maximum allowed size
    payload_size = parse_frame_header(local_payload);
    if (payload_size >= max_payload_size) {
      util::exception_location().raise<boost::system::system_error>(
          boost::asio::error::message_size,
          "frame payload size too large to receive");
    }
  }

  {
    const timer_guard read_timer_guard{timer_, read_timeout_};
    auto timed_read_result{co_await (
        boost::asio::async_read(
            stream_, payload_buffer,
            boost::asio::transfer_exactly(payload_size),
            boost::asio::as_tuple(boost::asio::use_awaitable)) ||
        timer_.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable)))};

    // if timer finished first, we consider it a timeout error
    if (timed_read_result.index() != 0UZ) {
      util::exception_location().raise<boost::system::system_error>(
          boost::asio::error::timed_out, "frame payload read timeout");
    }

    // extracting the result of async_read for payload
    const auto &payload_read_result{std::get<0UZ>(timed_read_result)};
    // extracting the error code from payload_read_result and throwing if there
    // was an error
    const auto payload_read_error_code{std::get<0UZ>(payload_read_result)};
    if (payload_read_error_code) {
      util::exception_location().raise<boost::system::system_error>(
          payload_read_error_code, "frame payload read error");
    }
    assert(std::size(local_payload) ==
           get_frame_header_length() + payload_size);
    assert(std::get<1UZ>(payload_read_result) == payload_size);

    payload.swap(local_payload);
  }
}

// a helper coroutine for writing with a timeout -
// throws on error
boost::asio::awaitable<void> session_frame_stream::async_write_mysql_frame(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const network_buffer_type &payload) {
  if (std::size(payload) >= max_payload_size) {
    util::exception_location().raise<boost::system::system_error>(
        boost::asio::error::message_size,
        "frame payload size too large to send");
  }

  using namespace boost::asio::experimental::awaitable_operators;

  const timer_guard write_timer_guard{timer_, write_timeout_};
  // timed_write_result is a variant of 2 results (one form async write, one
  // from timer)
  auto timed_write_result{co_await (
      boost::asio::async_write(
          stream_, boost::asio::buffer(payload),
          boost::asio::as_tuple(boost::asio::use_awaitable)) ||
      timer_.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable)))};

  // if timer finished first, we consider it a timeout error
  if (timed_write_result.index() != 0UZ) {
    util::exception_location().raise<boost::system::system_error>(
        boost::asio::error::timed_out, "frame write timeout");
  }

  // extracting the result of async_write
  const auto &write_result{std::get<0UZ>(timed_write_result)};
  // extracting the error code from async_write result and throwing if there was
  // an error
  const auto write_error_code{std::get<0UZ>(write_result)};
  if (write_error_code) {
    util::exception_location().raise<boost::system::system_error>(
        write_error_code, "frame write error");
  }
  assert(std::get<1UZ>(write_result) == std::size(payload));
}

// a helper coroutine for writing a collection of frames with a timeout -
// throws on error
boost::asio::awaitable<void> session_frame_stream::async_write_mysql_frames(
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const network_buffer_container &payloads) {
  for (const auto &payload : payloads) {
    co_await async_write_mysql_frame(payload);
  }
}

} // namespace minimysql
