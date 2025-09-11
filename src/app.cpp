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

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/try_lexical_convert.hpp>

#include "binsrv/basic_logger.hpp"
#include "binsrv/basic_storage_backend.hpp"
#include "binsrv/exception_handling_helpers.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/logger_factory.hpp"
#include "binsrv/main_config.hpp"
#include "binsrv/operation_mode_type.hpp"
#include "binsrv/storage.hpp"
#include "binsrv/storage_backend_factory.hpp"

#include "binsrv/event/code_type.hpp"
#include "binsrv/event/event.hpp"
#include "binsrv/event/flag_type.hpp"
#include "binsrv/event/protocol_traits_fwd.hpp"
#include "binsrv/event/reader_context.hpp"

#include "easymysql/connection.hpp"
#include "easymysql/connection_config.hpp"
#include "easymysql/core_error.hpp"
#include "easymysql/library.hpp"
// Needed for ssl_mode_type's operator <<
#include "easymysql/ssl_mode_type.hpp" // IWYU pragma: keep

#include "util/byte_span_fwd.hpp"
#include "util/command_line_helpers.hpp"
#include "util/ct_string.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/nv_tuple.hpp"

namespace {

using log_optional_string = std::optional<std::string>;

template <typename T> log_optional_string to_log_string(const T &value) {
  return boost::lexical_cast<std::string>(value);
}

log_optional_string to_log_string(bool value) {
  return {value ? "true" : "false"};
}

template <typename T>
log_optional_string to_log_string(const std::optional<T> &value) {
  if (!value.has_value()) {
    return {};
  }
  return to_log_string(value.value());
}

template <util::ct_string CTS, util::derived_from_named_value_tuple Config>
void log_config_param(binsrv::basic_logger &logger, const Config &config,
                      std::string_view label) {
  const auto opt_log_string{to_log_string(config.template get<CTS>())};
  if (opt_log_string.has_value()) {
    std::string msg{label};
    msg += ": ";
    msg += opt_log_string.value();
    logger.log(binsrv::log_severity::info, msg);
  }
}

void log_ssl_config_info(binsrv::basic_logger &logger,
                         const easymysql::ssl_config &ssl_config) {
  log_config_param<"mode">(logger, ssl_config, "SSL mode");
  log_config_param<"ca">(logger, ssl_config, "SSL ca");
  log_config_param<"capath">(logger, ssl_config, "SSL capath");
  log_config_param<"crl">(logger, ssl_config, "SSL crl");
  log_config_param<"crlpath">(logger, ssl_config, "SSL crlpath");
  log_config_param<"cert">(logger, ssl_config, "SSL cert");
  log_config_param<"key">(logger, ssl_config, "SSL key");
  log_config_param<"cipher">(logger, ssl_config, "SSL cipher");
}

void log_tls_config_info(binsrv::basic_logger &logger,
                         const easymysql::tls_config &tls_config) {
  log_config_param<"ciphersuites">(logger, tls_config, "TLS ciphersuites");
  log_config_param<"version">(logger, tls_config, "TLS version");
}

void log_connection_config_info(
    binsrv::basic_logger &logger,
    const easymysql::connection_config &connection_config) {
  std::string msg;
  msg = "mysql connection string: ";
  msg += connection_config.get_connection_string();
  logger.log(binsrv::log_severity::info, msg);

  log_config_param<"connect_timeout">(logger, connection_config,
                                      "mysql connect timeout (seconds)");
  log_config_param<"read_timeout">(logger, connection_config,
                                   "mysql read timeout (seconds)");
  log_config_param<"write_timeout">(logger, connection_config,
                                    "mysql write timeout (seconds)");

  const auto &optional_ssl_config{connection_config.get<"ssl">()};
  if (optional_ssl_config.has_value()) {
    log_ssl_config_info(logger, optional_ssl_config.value());
  }
  const auto &optional_tls_config{connection_config.get<"tls">()};
  if (optional_tls_config.has_value()) {
    log_tls_config_info(logger, optional_tls_config.value());
  }
}

void log_replication_config_info(
    binsrv::basic_logger &logger,
    const easymysql::replication_config &replication_config) {

  log_config_param<"server_id">(logger, replication_config,
                                "mysql replication server id");
  log_config_param<"idle_time">(logger, replication_config,
                                "mysql replication idle time (seconds)");
  log_config_param<"verify_checksum">(
      logger, replication_config, "mysql replication checksum verification");
}

void log_storage_info(binsrv::basic_logger &logger,
                      const binsrv::storage &storage) {
  std::string msg{};
  if (storage.has_current_binlog_name()) {
    msg = "binlog storage initialized at \"";
    msg += storage.get_current_binlog_name();
    msg += "\":";
    msg += std::to_string(storage.get_current_position());
  } else {
    msg = "binlog storage initialized on an empty directory";
  }
  logger.log(binsrv::log_severity::info, msg);
}

void log_library_info(binsrv::basic_logger &logger,
                      const easymysql::library &mysql_lib) {
  std::string msg{};
  msg = "mysql client version: ";
  msg += mysql_lib.get_readable_client_version();
  logger.log(binsrv::log_severity::info, msg);
}

void log_connection_info(binsrv::basic_logger &logger,
                         const easymysql::connection &connection) {
  std::string msg{};
  msg = "mysql server version: ";
  msg += connection.get_readable_server_version();
  logger.log(binsrv::log_severity::info, msg);

  logger.log(binsrv::log_severity::info,
             "mysql protocol version: " +
                 std::to_string(connection.get_protocol_version()));

  msg = "mysql server connection info: ";
  msg += connection.get_server_connection_info();
  logger.log(binsrv::log_severity::info, msg);

  msg = "mysql connection character set: ";
  msg += connection.get_character_set_name();
  logger.log(binsrv::log_severity::info, msg);
}

void log_replication_info(
    binsrv::basic_logger &logger, std::uint32_t server_id,
    std::string_view current_binlog_name, std::uint64_t current_binlog_position,
    easymysql::connection_replication_mode_type blocking_mode) {
  std::string msg{};
  msg = "replication info (server id ";
  msg += std::to_string(server_id);
  msg += ", ";
  msg += (current_binlog_name.empty() ? "<empty>" : current_binlog_name);
  msg += ":";
  msg += std::to_string(current_binlog_position);
  msg += ", ";
  msg += (blocking_mode == easymysql::connection_replication_mode_type::blocking
              ? "blocking"
              : "non-blocking");
  msg += ")";
  logger.log(binsrv::log_severity::info, msg);
}

void log_span_dump(binsrv::basic_logger &logger,
                   util::const_byte_span portion) {
  static constexpr std::size_t bytes_per_dump_line{16U};
  std::size_t offset{0U};
  while (offset < std::size(portion)) {
    std::ostringstream oss;
    oss << '[';
    oss << std::setfill('0') << std::hex;
    auto sub = portion.subspan(
        offset, std::min(bytes_per_dump_line, std::size(portion) - offset));
    for (auto current_byte : sub) {
      oss << ' ' << std::setw(2)
          << std::to_integer<std::uint16_t>(current_byte);
    }
    const std::size_t filler_length =
        (bytes_per_dump_line - std::size(sub)) * 3U;
    oss << std::setfill(' ') << std::setw(static_cast<int>(filler_length))
        << "";
    oss << " ] ";
    const auto &ctype_facet{
        std::use_facet<std::ctype<char>>(std::locale::classic())};

    for (auto current_byte : sub) {
      auto current_char{std::to_integer<char>(current_byte)};
      if (!ctype_facet.is(std::ctype_base::print, current_char)) {
        current_char = '.';
      }
      oss.put(current_char);
    }
    logger.log(binsrv::log_severity::trace, oss.str());
    offset += bytes_per_dump_line;
  }
}

void process_binlog_event(const binsrv::event::event &current_event,
                          util::const_byte_span portion,
                          binsrv::storage &storage, bool &skip_open_binlog) {
  const auto &current_common_header = current_event.get_common_header();
  const auto code = current_common_header.get_type_code();

  const auto is_artificial{current_common_header.get_flags().has_element(
      binsrv::event::flag_type::artificial)};
  const auto is_pseudo{current_common_header.get_next_event_position_raw() ==
                       0U};

  if (code == binsrv::event::code_type::rotate && is_artificial) {
    const auto &current_rotate_body =
        current_event.get_body<binsrv::event::code_type::rotate>();
    if (skip_open_binlog) {
      // we are supposed to get here after reconnection, so doing
      // basic integrity checks
      if (current_rotate_body.get_binlog() !=
          storage.get_current_binlog_name()) {
        util::exception_location().raise<std::logic_error>(
            "unexpected binlog name in artificial rotate event after "
            "reconnection");
      }
      const auto &current_rotate_post_header =
          current_event.get_post_header<binsrv::event::code_type::rotate>();
      if (current_rotate_post_header.get_position_raw() !=
          storage.get_current_position()) {
        util::exception_location().raise<std::logic_error>(
            "unexpected binlog position in artificial rotate event after "
            "reconnection");
      }

      skip_open_binlog = false;
    } else {
      // in case when the server was not shut down properly, it won't have
      // ROTATE or STOP event as the last one in the binlog, so here we
      // handle this case by closing the old binlog and opening a new one
      if (storage.is_binlog_open()) {
        storage.close_binlog();
      }
      storage.open_binlog(current_rotate_body.get_binlog());
    }
  }
  if (!is_artificial && !is_pseudo) {
    storage.write_event(portion);
  }
  if ((code == binsrv::event::code_type::rotate && !is_artificial) ||
      code == binsrv::event::code_type::stop) {
    storage.close_binlog();
  }
}

void receive_binlog_events(
    binsrv::operation_mode_type operation_mode,
    const volatile std::atomic_flag &termination_flag,
    binsrv::basic_logger &logger, const easymysql::library &mysql_lib,
    const easymysql::connection_config &connection_config,
    std::uint32_t server_id, bool verify_checksum, binsrv::storage &storage) {
  easymysql::connection connection{};
  try {
    connection = mysql_lib.create_connection(connection_config);
  } catch (const easymysql::core_error &) {
    if (operation_mode == binsrv::operation_mode_type::fetch) {
      throw;
    }
    logger.log(binsrv::log_severity::info,
               "unable to establish connection to mysql server");
    return;
  }

  logger.log(binsrv::log_severity::info,
             "established connection to mysql server");

  log_connection_info(logger, connection);

  const auto current_binlog_name{storage.get_current_binlog_name()};
  // if storage binlog name is detected to be empty (empty data directory), we
  // start streaming from the position 'magic_binlog_offset' (4)
  const auto current_binlog_position{current_binlog_name.empty()
                                         ? binsrv::event::magic_binlog_offset
                                         : storage.get_current_position()};

  const auto blocking_mode{
      operation_mode == binsrv::operation_mode_type::fetch
          ? easymysql::connection_replication_mode_type::non_blocking
          : easymysql::connection_replication_mode_type::blocking};

  try {
    connection.switch_to_replication(server_id, current_binlog_name,
                                     current_binlog_position, verify_checksum,
                                     blocking_mode);
  } catch (const easymysql::core_error &) {
    if (operation_mode == binsrv::operation_mode_type::fetch) {
      throw;
    }
    logger.log(binsrv::log_severity::info, "unable to switch to replication");
    return;
  }

  logger.log(binsrv::log_severity::info,
             std::string{"switched to replication (checksum "} +
                 (verify_checksum ? "enabled" : "disabled") + ")");

  log_replication_info(logger, server_id, current_binlog_name,
                       current_binlog_position, blocking_mode);

  // Network streams are requested with COM_BINLOG_DUMP and
  // each Binlog Event response is prepended with 00 OK-byte.
  static constexpr std::byte expected_event_packet_prefix{'\0'};

  util::const_byte_span portion;

  binsrv::event::reader_context context{connection.get_server_version(),
                                        verify_checksum};

  // if binlog is still open, there is no sense to close it and re-open
  // instead, we will just instruct this loop to process the
  // very first artificial rotate event in a special way
  bool skip_open_binlog{storage.is_binlog_open()};
  bool fetch_result{};

  while (!termination_flag.test() &&
         (fetch_result = connection.fetch_binlog_event(portion)) &&
         !portion.empty()) {
    if (portion[0] != expected_event_packet_prefix) {
      util::exception_location().raise<std::runtime_error>(
          "unexpected event prefix");
    }
    portion = portion.subspan(1U);
    logger.log(binsrv::log_severity::info,
               "fetched " + std::to_string(std::size(portion)) +
                   "-byte(s) event from binlog");

    // TODO: just for redirection to another byte stream we need to parse
    //       the ROTATE and FORMAT_DESCRIPTION events only, every other one
    //       can be just considered as a data portion (unless we want to do
    //       basic integrity checks like event sizes / position and CRC)
    const binsrv::event::event current_event{context, portion};
    logger.log(binsrv::log_severity::debug,
               "Parsed event:\n" +
                   boost::lexical_cast<std::string>(current_event));
    log_span_dump(logger, portion);

    process_binlog_event(current_event, portion, storage, skip_open_binlog);
  }
  if (termination_flag.test()) {
    logger.log(binsrv::log_severity::info,
               "fetching binlog events loop terminated by signal");
    return;
  }
  if (fetch_result) {
    logger.log(binsrv::log_severity::info,
               "fetched everything and disconnected");
    return;
  }
  if (operation_mode == binsrv::operation_mode_type::fetch) {
    util::exception_location().raise<std::logic_error>(
        "fetch operation did not reach EOF reading binlog events");
  }
  logger.log(binsrv::log_severity::info,
             "timed out waiting for events and disconnected");
}

bool wait_for_interruptable(std::uint32_t idle_time_seconds,
                            const volatile std::atomic_flag &termination_flag) {
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
       sleep_iteration < idle_time_seconds && !termination_flag.test();
       ++sleep_iteration) {
    std::this_thread::sleep_for(std::chrono::seconds(1U));
  }
  return !termination_flag.test();
}

// since c++20 it is no longer needed to initialize std::atomic_flag with
// ATOMIC_FLAG_INIT as this flag is modified from a signal handler it is marked
// as volatile to make sure optimizer do optimizations which will be unsafe for
// this scenario
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::atomic_flag global_termination_flag{};

} // anonymous namespace
extern "C" void custom_signal_handler(int /*signo*/) {
  global_termination_flag.test_and_set();
}

int main(int argc, char *argv[]) {
  using namespace std::string_literals;

  int exit_code = EXIT_FAILURE;

  const auto cmd_args = util::to_command_line_agg_view(argc, argv);
  const auto number_of_cmd_args = std::size(cmd_args);
  const auto executable_name = util::extract_executable_name(cmd_args);

  static constexpr std::size_t expected_number_of_cmd_args{3U};
  binsrv::operation_mode_type operation_mode{
      binsrv::operation_mode_type::delimiter};
  auto cmd_args_validated{
      number_of_cmd_args == expected_number_of_cmd_args &&
      boost::conversion::try_lexical_convert(cmd_args[1], operation_mode) &&
      operation_mode != binsrv::operation_mode_type::delimiter};

  if (!cmd_args_validated) {
    std::cerr << "usage: " << executable_name
              << " (fetch|pull)) <json_config_file>\n";
    return exit_code;
  }
  binsrv::basic_logger_ptr logger;

  try {
    static constexpr auto default_log_level = binsrv::log_severity::trace;

    const binsrv::logger_config initial_logger_config{
        {{default_log_level}, {""}}};

    logger = binsrv::logger_factory::create(initial_logger_config);
    // logging with "delimiter" level has the highest priority and empty label
    logger->log(binsrv::log_severity::delimiter,
                '"' + executable_name + '"' +
                    " started with the following command line arguments:");
    logger->log(binsrv::log_severity::delimiter,
                util::get_readable_command_line_arguments(cmd_args));

    assert(operation_mode == binsrv::operation_mode_type::fetch ||
           operation_mode == binsrv::operation_mode_type::pull);
    std::string msg;
    msg = '\'';
    msg += boost::lexical_cast<std::string>(operation_mode);
    msg += "' operation mode specified";
    logger->log(binsrv::log_severity::delimiter, msg);

    binsrv::main_config_ptr config;
    assert(number_of_cmd_args == expected_number_of_cmd_args);
    logger->log(binsrv::log_severity::delimiter,
                "reading configuration from the JSON file.");
    config = std::make_shared<binsrv::main_config>(cmd_args[2]);
    assert(config);

    const auto &logger_config = config->root().get<"logger">();
    if (!logger_config.has_file()) {
      logger->set_min_level(logger_config.get<"level">());
    } else {
      logger->log(binsrv::log_severity::delimiter,
                  "redirecting logging to \"" + logger_config.get<"file">() +
                      "\"");
      auto new_logger = binsrv::logger_factory::create(logger_config);
      std::swap(logger, new_logger);
    }

    const auto log_level_label =
        binsrv::to_string_view(logger->get_min_level());
    logger->log(binsrv::log_severity::delimiter,
                "logging level set to \""s + std::string{log_level_label} +
                    '"');

    // setting custom SIGINT and SIGTERM signal handlers
    if (std::signal(SIGTERM, &custom_signal_handler) == SIG_ERR) {
      util::exception_location().raise<std::logic_error>(
          "cannot set custom signal handler for SIGTERM");
    }
    if (std::signal(SIGINT, &custom_signal_handler) == SIG_ERR) {
      util::exception_location().raise<std::logic_error>(
          "cannot set custom signal handler for SIGINT");
    }

    logger->log(binsrv::log_severity::info,
                "set custom handlers for SIGINT and SIGTERM signals");
    const volatile std::atomic_flag &termination_flag{global_termination_flag};

    const auto &storage_config = config->root().get<"storage">();
    auto storage_backend{
        binsrv::storage_backend_factory::create(storage_config)};
    msg = "created binlog storage backend: ";
    msg += storage_backend->get_description();
    logger->log(binsrv::log_severity::info, msg);

    binsrv::storage storage{std::move(storage_backend)};
    logger->log(binsrv::log_severity::info,
                "created binlog storage with the provided backend");

    log_storage_info(*logger, storage);

    const auto &connection_config = config->root().get<"connection">();
    log_connection_config_info(*logger, connection_config);

    const auto &replication_config = config->root().get<"replication">();
    log_replication_config_info(*logger, replication_config);

    const auto server_id{replication_config.get<"server_id">()};
    const auto idle_time_seconds{replication_config.get<"idle_time">()};
    const auto verify_checksum{replication_config.get<"verify_checksum">()};

    const easymysql::library mysql_lib;
    logger->log(binsrv::log_severity::info, "initialized mysql client library");

    log_library_info(*logger, mysql_lib);

    receive_binlog_events(operation_mode, termination_flag, *logger, mysql_lib,
                          connection_config, server_id, verify_checksum,
                          storage);

    if (operation_mode == binsrv::operation_mode_type::pull) {
      std::size_t iteration_number{1U};
      while (!termination_flag.test()) {
        msg = "entering idle mode for ";
        msg += std::to_string(idle_time_seconds);
        msg += " seconds";
        logger->log(binsrv::log_severity::info, msg);

        if (!wait_for_interruptable(idle_time_seconds, termination_flag)) {
          break;
        }

        msg = "awoke after sleeping and trying to reconnect (iteration ";
        msg += std::to_string(iteration_number);
        msg += ')';
        logger->log(binsrv::log_severity::info, msg);

        receive_binlog_events(operation_mode, termination_flag, *logger,
                              mysql_lib, connection_config, server_id,
                              verify_checksum, storage);
        ++iteration_number;
      }
    }

    if (termination_flag.test()) {
      logger->log(
          binsrv::log_severity::info,
          "successfully shut down after receiving a termination signal");
    } else {
      logger->log(
          binsrv::log_severity::info,
          "successfully shut down after finishing the requested operation");
    }

    exit_code = EXIT_SUCCESS;
  } catch (...) {
    handle_std_exception(logger);
  }

  return exit_code;
}
