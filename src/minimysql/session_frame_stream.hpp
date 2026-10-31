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

#ifndef MINIMYSQL_SESSION_FRAME_STREAM_HPP
#define MINIMYSQL_SESSION_FRAME_STREAM_HPP

#include "minimysql/session_frame_stream_fwd.hpp" // IWYU pragma: export

#include <chrono>
#include <string>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include <boost/asio/ts/netfwd.hpp>

#pragma GCC diagnostic pop

#include "minimysql/upgradable_stream.hpp"

namespace minimysql {

class session_frame_stream {
public:
  // passing by value as we are going to move from these parameters
  session_frame_stream(
      boost::asio::basic_stream_socket<boost::asio::ip::tcp> socket,
      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      std::chrono::steady_clock::duration read_timeout,
      std::chrono::steady_clock::duration write_timeout);

  session_frame_stream(const session_frame_stream &) = delete;
  session_frame_stream(session_frame_stream &&) = default;
  session_frame_stream &operator=(const session_frame_stream &) = delete;
  session_frame_stream &operator=(session_frame_stream &&) = default;

  ~session_frame_stream() = default;

  [[nodiscard]] const std::string &
  get_readable_remote_endpoint() const noexcept {
    return readable_remote_endpoint_;
  }

  boost::asio::awaitable<void>
  async_upgrade_to_secure(boost::asio::ssl::context &context,
                          boost::asio::ssl::stream_base::handshake_type role);

  boost::asio::awaitable<void> async_secure_shutdown();

  // a helper coroutine for reading MySQL frame with a timeout -
  // throws on error
  boost::asio::awaitable<void> async_read_mysql_frame(
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
      network_buffer_type &payload);

  // a helper coroutine for writing with a timeout - throws on error
  boost::asio::awaitable<void> async_write_mysql_frame(
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
      const network_buffer_type &payload);

  // a helper coroutine for writing a collection of frames with a timeout -
  // throws on error
  boost::asio::awaitable<void> async_write_mysql_frames(
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
      const network_buffer_container &payloads);

private:
  class timer_guard;

  upgradable_stream stream_;
  boost::asio::steady_timer timer_;
  std::chrono::steady_clock::duration read_timeout_;
  std::chrono::steady_clock::duration write_timeout_;
  std::string readable_remote_endpoint_;
};

} // namespace minimysql

#endif // MINIMYSQL_SESSION_FRAME_STREAM_HPP
