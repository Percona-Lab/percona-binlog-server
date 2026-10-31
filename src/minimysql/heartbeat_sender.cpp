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

#include "minimysql/heartbeat_sender.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <string_view>
#include <utility>

#include <boost/asio/awaitable.hpp>

#include "binsrv/basic_logger.hpp"
#include "binsrv/log_severity.hpp"

#include "minimysql/connection_context.hpp"
#include "minimysql/session_frame_stream.hpp"

#include "operations/sender_context.hpp"

#include "util/byte_span.hpp"

namespace minimysql {

heartbeat_sender::heartbeat_sender(binsrv::basic_logger_ptr logger,
                                   std::chrono::nanoseconds period,
                                   session_frame_stream &stream,
                                   connection_context &context,
                                   operations::sender_context &sender_ctx)
    : logger_{std::move(logger)}, stream_{&stream}, context_{&context},
      sender_ctx_{&sender_ctx}, period_{period} {}

void heartbeat_sender::reset() noexcept {
  if (!enabled()) {
    return;
  }
  last_reset_ = std::chrono::steady_clock::now();
}

std::chrono::nanoseconds heartbeat_sender::wait_cap(
    std::chrono::nanoseconds poll_interval) const noexcept {
  if (!enabled()) {
    return poll_interval;
  }
  const auto elapsed{std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - last_reset_)};
  if (elapsed >= period_) {
    return std::chrono::nanoseconds::zero();
  }
  return std::min(poll_interval, period_ - elapsed);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"

boost::asio::awaitable<void> heartbeat_sender::send_if_due() {
  if (!enabled()) {
    co_return;
  }
  const auto elapsed{std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - last_reset_)};
  if (elapsed < period_) {
    co_return;
  }

  const auto &remote_endpoint_str{stream_->get_readable_remote_endpoint()};
  util::const_byte_span heartbeat_data{};
  if (!sender_ctx_->generate_heartbeat(heartbeat_data)) {
    // the stream has not reached a point inside a binlog file where a heartbeat
    // can be reported yet (e.g. the format description event has not been
    // collected). Skip this beat and restart the countdown so we retry after a
    // full period instead of spinning or dropping the connection.
    logger_->log_format(binsrv::log_severity::debug,
                        "net    : heartbeat due but stream not yet positioned "
                        "in a binlog file, skipping for {}",
                        remote_endpoint_str);
    reset();
    co_return;
  }
  const auto heartbeat_frame{
      context_->generate_encoded_binlog_event(heartbeat_data)};
  logger_->log_format(binsrv::log_severity::info,
                      "net    : generated heartbeat event packet for {}",
                      remote_endpoint_str);
  co_await stream_->async_write_mysql_frame(heartbeat_frame);
  logger_->log_format(binsrv::log_severity::debug,
                      "net    : sent server heartbeat event ({} bytes to {})",
                      std::size(heartbeat_frame), remote_endpoint_str);
  reset();
}

#pragma GCC diagnostic pop

} // namespace minimysql
