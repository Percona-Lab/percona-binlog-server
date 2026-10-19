// Copyright (c) 2026 Percona and/or its affiliates.
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

#ifndef MINIMYSQL_HEARTBEAT_SENDER_HPP
#define MINIMYSQL_HEARTBEAT_SENDER_HPP

#include "minimysql/heartbeat_sender_fwd.hpp" // IWYU pragma: export

#include <chrono>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>

#include <boost/asio/ts/netfwd.hpp>

#include "binsrv/basic_logger_fwd.hpp"

#include "minimysql/connection_context_fwd.hpp"

#include "operations/sender_context_fwd.hpp"

namespace minimysql {

// Watchdog that emits HEARTBEAT_LOG_EVENT_V2 packets to a replica while a
// blocking binlog dump sits idle at EOF, so the replica can tell a quiet
// source from a stalled one. It is bound at construction to the connection it
// serves (socket, connection and sender contexts), all of which must outlive
// it. The dump loop calls reset() whenever it sends anything and send_if_due()
// on every idle wake-up; a heartbeat is emitted only once a full period has
// elapsed since the last reset. A zero period disables heartbeats entirely.
class heartbeat_sender {
public:
  heartbeat_sender(
      binsrv::basic_logger_ptr logger, std::chrono::nanoseconds period,
      std::chrono::seconds write_timeout,
      boost::asio::basic_stream_socket<boost::asio::ip::tcp> &socket,
      connection_context &context, operations::sender_context &sender_ctx,
      std::string_view remote_endpoint);

  [[nodiscard]] bool enabled() const noexcept { return period_.count() > 0; }

  // restart the countdown from now
  void reset() noexcept;

  // Sends a heartbeat if the period has elapsed since the last reset (and
  // heartbeats are enabled), restarting the countdown when it does. Does
  // nothing otherwise, including when the stream is not yet positioned inside a
  // binlog file and has no valid position to report - in that case the
  // countdown is restarted so the next attempt waits a full period. A write
  // failure is reported by throwing, as elsewhere in the dump loop.
  boost::asio::awaitable<void> send_if_due();

  // The longest the caller may sleep before it must call send_if_due() again,
  // never longer than 'poll_interval'.
  [[nodiscard]] std::chrono::nanoseconds
  wait_cap(std::chrono::nanoseconds poll_interval) const noexcept;

private:
  binsrv::basic_logger_ptr logger_;
  boost::asio::basic_stream_socket<boost::asio::ip::tcp> *socket_;
  connection_context *context_;
  operations::sender_context *sender_ctx_;
  std::string remote_endpoint_;
  std::chrono::nanoseconds period_;
  std::chrono::seconds write_timeout_;
  std::chrono::steady_clock::time_point last_reset_{
      std::chrono::steady_clock::now()};
};

} // namespace minimysql

#endif // MINIMYSQL_HEARTBEAT_SENDER_HPP
