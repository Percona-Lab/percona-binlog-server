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
#include "operations/pull_operation.hpp"

#include <cassert>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

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

#include "minimysql/network_service.hpp"
#include "minimysql/ssl_acceptor_context.hpp"

#include "operations/basic_operation.hpp"
#include "operations/collector_context.hpp"
#include "operations/mode_type.hpp"

#include "util/command_line_helpers_fwd.hpp"

namespace operations {

namespace {

bool wait_for_interruptable(std::uint32_t idle_time_seconds,
                            const boost::asio::io_context &io_ctx) {
  // TODO: rework this with boost::asio::steady_timer and async_wait()

  // instead of
  // 'std::this_thread::sleep_for(std::chrono::seconds(idle_time_seconds))'
  // we do 'std::this_thread::sleep_for(1s)' '<idle_time_seconds>' times
  // in a loop also checking for termination condition

  // standard pattern with declaring an instance of
  // std::conditional_variable and waiting for it (for
  // '<idle_time_seconds>' seconds) to be notified from the signal handler
  // can be dangerous as the chances of signal handler being called on the
  // same thread as this one ('main()') are pretty big.
  for (std::uint32_t sleep_iteration{0U};
       sleep_iteration < idle_time_seconds && !io_ctx.stopped();
       ++sleep_iteration) {
    std::this_thread::sleep_for(std::chrono::seconds(1U));
  }
  return !io_ctx.stopped();
}

} // anonymous namespace

generic_operation<mode_type::pull>::generic_operation(
    util::command_line_arg_view cmd_args)
    : basic_operation{cmd_args, expected_number_of_arguments} {}

[[nodiscard]] bool generic_operation<mode_type::pull>::execute() const {
  static constexpr std::uint16_t listening_port{3307};

  static constexpr std::string_view default_username{"rpl"};
  static constexpr std::string_view default_password{"password"};

  bool result{false};

  binsrv::basic_logger_ptr logger;

  try {
    logger = collector_context::initialize_console_logger(get_cmd_args());
    const binsrv::main_config_ptr config{
        std::make_shared<binsrv::main_config>(get_config_file_path())};
    collector_context::reinitialize_logger_from_config(logger, *config);

    logger->log(binsrv::log_severity::delimiter,
                "'pull' operation mode specified");

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
        easymysql::connection_replication_mode_type::blocking, config, logger};

    // The 'pbs_listener' JSON config block carries per-listener options:
    //   * RSA key pair for the caching_sha2_password full-authentication
    //     branch (PBS-33) - forwarded to network_service as-is. Empty
    //     paths mean the authenticator has no RSA key material and any
    //     0x04 attempt fails per-session.
    //   * TLS cert / key pair for the optional TLS listener (PBS-31) -
    //     when both non-empty we build an ssl_acceptor_context here so
    //     any load failure (bad file, mismatched pair) surfaces before
    //     network_service is instantiated, matching the ownership-
    //     transfer contract on the network_service constructor.
    // The block itself is optional; when absent we forward empty views
    // for RSA and a null ssl_ctx for TLS.
    std::string_view server_rsa_public_key_path{};
    std::string_view server_rsa_private_key_path{};
    std::unique_ptr<minimysql::ssl_acceptor_context> ssl_ctx;
    const auto &optional_listener{config->root().get<"pbs_listener">()};
    if (optional_listener.has_value()) {
      server_rsa_public_key_path =
          optional_listener->get<"rsa_public_key_path">();
      server_rsa_private_key_path =
          optional_listener->get<"rsa_private_key_path">();

      const auto &ssl_cert_path{optional_listener->get<"ssl_cert_path">()};
      const auto &ssl_key_path{optional_listener->get<"ssl_key_path">()};
      if (!ssl_cert_path.empty() && !ssl_key_path.empty()) {
        ssl_ctx = std::make_unique<minimysql::ssl_acceptor_context>(
            ssl_cert_path, ssl_key_path);
        logger->log(binsrv::log_severity::info,
                    "SSL enabled for the Binlog Server listener (cert='" +
                        ssl_cert_path + "', key='" + ssl_key_path + "')");
      }
    }
    if (!ssl_ctx) {
      logger->log(binsrv::log_severity::info,
                  "SSL disabled for the Binlog Server listener (no "
                  "'pbs_listener.ssl_cert_path' / 'ssl_key_path' in config)");
    }

    const minimysql::network_service service(
        io_ctx, listening_port, default_username, default_password,
        server_rsa_public_key_path, server_rsa_private_key_path,
        std::move(ssl_ctx));

    const auto idle_time_seconds{
        config->root().get<"replication">().get<"idle_time">()};

    std::exception_ptr operation_exception{};
    {
      const std::jthread operation_thread{[&io_ctx, &collector_ctx, &logger,
                                           &operation_exception,
                                           idle_time_seconds]() {
        // 'io_ctx.stop()' should be called regardless of whether the
        // 'receive_binlog_events()' method throws or returns normally

        // it is also OK if this 'io_ctx.stop()' method is called multiple
        // times (in the signal handler and here)
        try {
          collector_ctx.receive_binlog_events(io_ctx);

          std::string msg;
          auto iteration_number{1UZ};
          while (!io_ctx.stopped()) {
            msg = "entering idle mode for ";
            msg += std::to_string(idle_time_seconds);
            msg += " seconds";
            logger->log(binsrv::log_severity::info, msg);

            if (!wait_for_interruptable(idle_time_seconds, io_ctx)) {
              break;
            }

            msg = "awoke after sleeping and trying to reconnect (iteration ";
            msg += std::to_string(iteration_number);
            msg += ')';
            logger->log(binsrv::log_severity::info, msg);

            collector_ctx.receive_binlog_events(io_ctx);
            ++iteration_number;
          }
        } catch (...) {
          operation_exception = std::current_exception();
        }
        io_ctx.stop();
      }};

      io_ctx.run();
    }

    // check if the operation thread encountered an exception
    if (operation_exception) {
      std::rethrow_exception(operation_exception);
    }

    logger->log(binsrv::log_severity::info,
                "successfully shut down after receiving a termination signal");

    result = true;
  } catch (...) {
    handle_std_exception(logger);
  }
  return result;
}

} // namespace operations
