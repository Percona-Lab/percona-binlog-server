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

#include "minimysql/network_io_operations.hpp"

#include <cassert>
#include <chrono>
#include <iterator>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/read.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"

#include <boost/asio/steady_timer.hpp>

#pragma GCC diagnostic pop

#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/system/system_error.hpp>

#include "minimysql/connection_context_fwd.hpp"

namespace minimysql {

// as this coroutine is always used with co_await, it is absolutely safe to
// pass arguments by reference here
boost::asio::awaitable<void> async_read_mysql_frame(
    // a helper coroutine for reading MySQL frame with a timeout - returns a
    // tuple of (error_code, bytes_transferred)
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    network_buffer_type &payload, std::chrono::steady_clock::duration timeout) {
  using namespace boost::asio::experimental::awaitable_operators;

  network_buffer_type local_payload{};
  auto payload_buffer{boost::asio::dynamic_buffer(local_payload)};

  boost::asio::steady_timer read_timer{socket.get_executor(), timeout};
  // timed_read_result is a variant of 2 results (one form async read, one from
  // timer)
  auto timed_read_result{
      co_await (boost::asio::async_read(
                    socket, payload_buffer,
                    boost::asio::transfer_exactly(get_frame_header_length()),
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

  assert(std::size(local_payload) == get_frame_header_length());
  assert(std::get<1UZ>(header_read_result) == get_frame_header_length());

  // checking the payload size from the header and throwing if it is larger than
  // our maximum allowed size
  auto payload_size{parse_frame_header(local_payload)};
  if (payload_size >= max_payload_size) {
    throw boost::system::system_error{
        boost::asio::error::message_size,
        "frame payload size too large to receive"};
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
  assert(std::size(local_payload) == get_frame_header_length() + payload_size);
  assert(std::get<1UZ>(payload_read_result) == payload_size);

  payload.swap(local_payload);
}

// a helper coroutine for writing with a timeout -
// throws on error
boost::asio::awaitable<void> async_write_mysql_frame(
    // as this coroutine is always used with co_await, it is absolutely safe to
    // pass arguments by reference here
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const network_buffer_type &payload,
    std::chrono::steady_clock::duration timeout) {
  if (std::size(payload) >= max_payload_size) {
    throw boost::system::system_error{boost::asio::error::message_size,
                                      "frame payload size too large to send"};
  }

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
boost::asio::awaitable<void> async_write_mysql_frames(
    // as this coroutine is always used with co_await, it is absolutely safe to
    // pass arguments by reference here
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::ip::tcp::socket &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const network_buffer_container &payloads,
    std::chrono::steady_clock::duration timeout) {
  for (const auto &payload : payloads) {
    co_await async_write_mysql_frame(socket, payload, timeout);
  }
}

} // namespace minimysql
