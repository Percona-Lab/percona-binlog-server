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

#ifndef MINIMYSQL_NETWORK_IO_OPERATIONS_HPP
#define MINIMYSQL_NETWORK_IO_OPERATIONS_HPP

#include "minimysql/network_io_operations_fwd.hpp" // IWYU pragma: export

#include <cassert>
#include <chrono>
#include <tuple>

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

#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/system/system_error.hpp>

#include "minimysql/connection_context_fwd.hpp"

namespace minimysql {

// Reads exactly one MySQL frame (header + payload) from `socket` with a
// combined timeout for header and payload.
//
// `Socket` may be any Boost.Asio AsyncReadStream — used with
// `boost::asio::ip::tcp::socket` for plaintext and
// `boost::asio::ssl::stream<...>` for TLS-upgraded connections. As this
// coroutine is always used with `co_await`, it is safe to pass arguments by
// reference here.
// NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
template <typename Socket>
boost::asio::awaitable<void>
async_read_mysql_frame(Socket &socket, network_buffer_type &payload,
                       std::chrono::steady_clock::duration timeout) {
  // NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)
  using namespace boost::asio::experimental::awaitable_operators;

  network_buffer_type local_payload{};
  auto payload_buffer{boost::asio::dynamic_buffer(local_payload)};

  boost::asio::steady_timer read_timer{socket.get_executor(), timeout};
  auto timed_read_result{
      co_await (boost::asio::async_read(
                    socket, payload_buffer,
                    boost::asio::transfer_exactly(get_frame_header_length()),
                    boost::asio::as_tuple(boost::asio::use_awaitable)) ||
                read_timer.async_wait(
                    boost::asio::as_tuple(boost::asio::use_awaitable)))};

  if (timed_read_result.index() != 0UZ) {
    throw boost::system::system_error{boost::asio::error::timed_out,
                                      "frame header read timeout"};
  }

  const auto &header_read_result{std::get<0UZ>(timed_read_result)};

  const auto header_read_error_code{std::get<0UZ>(header_read_result)};
  if (header_read_error_code) {
    throw boost::system::system_error{header_read_error_code,
                                      "frame header read error"};
  }

  assert(std::size(local_payload) == get_frame_header_length());
  assert(std::get<1UZ>(header_read_result) == get_frame_header_length());

  auto payload_size{parse_frame_header(local_payload)};
  if (payload_size >= max_payload_size) {
    throw boost::system::system_error{
        boost::asio::error::message_size,
        "frame payload size too large to receive"};
  }

  read_timer.expires_after(timeout);
  timed_read_result = co_await (
      boost::asio::async_read(
          socket, payload_buffer, boost::asio::transfer_exactly(payload_size),
          boost::asio::as_tuple(boost::asio::use_awaitable)) ||
      read_timer.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable)));

  if (timed_read_result.index() != 0UZ) {
    throw boost::system::system_error{boost::asio::error::timed_out,
                                      "frame payload read timeout"};
  }

  const auto &payload_read_result{std::get<0UZ>(timed_read_result)};
  const auto payload_read_error_code{std::get<0UZ>(payload_read_result)};
  if (payload_read_error_code) {
    throw boost::system::system_error{payload_read_error_code,
                                      "frame payload read error"};
  }
  assert(std::size(local_payload) == get_frame_header_length() + payload_size);
  assert(std::get<1UZ>(payload_read_result) == payload_size);

  payload.swap(local_payload);
}

// Writes one MySQL frame (a pre-encoded header + payload buffer) to `socket`
// with a timeout, throwing on error.
// NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
template <typename Socket>
boost::asio::awaitable<void>
async_write_mysql_frame(Socket &socket, const network_buffer_type &payload,
                        std::chrono::steady_clock::duration timeout) {
  // NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)
  if (std::size(payload) >= max_payload_size) {
    throw boost::system::system_error{boost::asio::error::message_size,
                                      "frame payload size too large to send"};
  }

  using namespace boost::asio::experimental::awaitable_operators;

  boost::asio::steady_timer write_timer{socket.get_executor(), timeout};
  auto timed_write_result{
      co_await (boost::asio::async_write(
                    socket, boost::asio::buffer(payload),
                    boost::asio::as_tuple(boost::asio::use_awaitable)) ||
                write_timer.async_wait(
                    boost::asio::as_tuple(boost::asio::use_awaitable)))};

  if (timed_write_result.index() != 0UZ) {
    throw boost::system::system_error{boost::asio::error::timed_out,
                                      "frame write timeout"};
  }

  const auto &write_result{std::get<0UZ>(timed_write_result)};
  const auto write_error_code{std::get<0UZ>(write_result)};
  if (write_error_code) {
    throw boost::system::system_error{write_error_code, "frame write error"};
  }
  assert(std::get<1UZ>(write_result) == std::size(payload));
}

// Writes each frame in `payloads` sequentially with the same timeout budget
// applied to every frame.
// NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
template <typename Socket>
boost::asio::awaitable<void>
async_write_mysql_frames(Socket &socket,
                         const network_buffer_container &payloads,
                         std::chrono::steady_clock::duration timeout) {
  // NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)
  for (const auto &payload : payloads) {
    co_await async_write_mysql_frame(socket, payload, timeout);
  }
}

} // namespace minimysql

#endif // MINIMYSQL_NETWORK_IO_OPERATIONS_HPP
