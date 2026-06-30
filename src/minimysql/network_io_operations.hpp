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

#include <array>
#include <chrono>
#include <string>

#include <boost/asio/awaitable.hpp>

#include <boost/asio/ts/netfwd.hpp>

namespace minimysql {

// a helper coroutine for reading MySQL frame with a timeout - returns a tuple
// of (error_code, bytes_transferred)
boost::asio::awaitable<void> async_read_mysql_frame(
    // as this coroutine is always used with co_await, it is absolutely safe to
    // pass arguments by reference here
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::basic_stream_socket<boost::asio::ip::tcp> &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    network_buffer_type &payload, std::chrono::steady_clock::duration timeout);

// as this coroutine is always used with co_await, it is absolutely safe to
// pass arguments by reference here
boost::asio::awaitable<void> async_write_mysql_frame(
    // a helper coroutine for writing with a timeout -
    // throws on error
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::basic_stream_socket<boost::asio::ip::tcp> &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const network_buffer_type &payload,
    std::chrono::steady_clock::duration timeout);

// as this coroutine is always used with co_await, it is absolutely safe to
// pass arguments by reference here
boost::asio::awaitable<void> async_write_mysql_frames(
    // a helper coroutine for writing a collection of frames with a timeout -
    // throws on error
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    boost::asio::basic_stream_socket<boost::asio::ip::tcp> &socket,
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    const network_buffer_container &payloads,
    std::chrono::steady_clock::duration timeout);

} // namespace minimysql

#endif // MINIMYSQL_NETWORK_IO_OPERATIONS_HPP
