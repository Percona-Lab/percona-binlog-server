// Copyright (c) 2023-2024 Percona and/or its affiliates.
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
#include "operations/fetch_operation.hpp"

#include <cassert>
#include <csignal>
#include <cstddef>
#include <exception>
#include <memory>
#include <thread>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#pragma GCC diagnostic pop

#include "binsrv/basic_logger.hpp"
#include "binsrv/exception_handling_helpers.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/main_config.hpp"

#include "easymysql/connection_fwd.hpp"

#include "operations/basic_operation.hpp"
#include "operations/collector_context.hpp"
#include "operations/mode_type.hpp"

#include "util/command_line_helpers_fwd.hpp"

namespace operations {

generic_operation<mode_type::fetch>::generic_operation(
    util::command_line_arg_view cmd_args)
    : basic_operation{cmd_args, expected_number_of_arguments} {}

[[nodiscard]] bool generic_operation<mode_type::fetch>::execute() const {
  bool result{false};

  binsrv::basic_logger_ptr logger;

  try {
    logger = collector_context::initialize_console_logger(get_cmd_args());
    const binsrv::main_config_ptr config{
        std::make_shared<binsrv::main_config>(get_config_file_path())};
    collector_context::reinitialize_logger_from_config(logger, *config);

    logger->log(binsrv::log_severity::delimiter,
                "'fetch' operation mode specified");

    boost::asio::io_context io_ctx;
    boost::asio::signal_set signals(io_ctx, SIGINT, SIGTERM);
    // calling this 'async_wait()' method on a 'signal_set' created on the
    // same 'io_context' will make sure that the 'io_context::run()' method
    // will not return immediately and will wait for the signal to be received
    // or for the 'io_context::stop()' method to be called explicitly
    signals.async_wait([&io_ctx](auto, auto) { io_ctx.stop(); });
    logger->log(binsrv::log_severity::info,
                "set custom handlers for SIGINT and SIGTERM signals");

    operations::collector_context collector_ctx{
        easymysql::connection_replication_mode_type::non_blocking, config,
        logger};

    std::exception_ptr operation_exception{};
    bool operation_result{};
    {
      const std::jthread operation_thread{
          [&io_ctx, &collector_ctx, &operation_exception, &operation_result]() {
            // 'io_ctx.stop()' should be called regardless of whether the
            // 'receive_binlog_events()' method throws or returns normally

            // it is also OK if this 'io_ctx.stop()' method is called multiple
            // times (in the signal handler and here)
            try {
              operation_result = collector_ctx.receive_binlog_events(io_ctx);
            } catch (...) {
              operation_exception = std::current_exception();
            }
            io_ctx.stop();
          }};

      io_ctx.run();
    }
    if (operation_exception) {
      std::rethrow_exception(operation_exception);
    }

    if (operation_result) {
      logger->log(binsrv::log_severity::info,
                  "successfully fetched everything and disconnected");
      result = true;
    } else {
      logger->log(binsrv::log_severity::error,
                  "fetching binlog events loop did not reach EOF");
    }
  } catch (...) {
    handle_std_exception(logger);
  }
  return result;
}

} // namespace operations
