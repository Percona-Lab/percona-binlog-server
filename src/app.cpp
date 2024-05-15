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
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
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

#include "util/byte_span_fwd.hpp"
#include "util/command_line_helpers.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/nv_tuple.hpp"

namespace {

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
      storage.open_binlog(current_rotate_body.get_binlog());
    }
  }
  if (!is_artificial && !is_pseudo) {
    storage.write_event(portion);
  }
  if (code == binsrv::event::code_type::rotate && !is_artificial) {
    storage.close_binlog();
  }
}

void receive_binlog_events(
    binsrv::operation_mode_type operation_mode, binsrv::basic_logger &logger,
    const easymysql::library &mysql_lib,
    const easymysql::connection_config &connection_config,
    std::uint32_t server_id, binsrv::storage &storage) {
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
                                     current_binlog_position, blocking_mode);
  } catch (const easymysql::core_error &) {
    if (operation_mode == binsrv::operation_mode_type::fetch) {
      throw;
    }
    logger.log(binsrv::log_severity::info, "unable to switch to replication");
    return;
  }

  logger.log(binsrv::log_severity::info, "switched to replication");

  log_replication_info(logger, server_id, current_binlog_name,
                       current_binlog_position, blocking_mode);

  // Network streams are requested with COM_BINLOG_DUMP and
  // each Binlog Event response is prepended with 00 OK-byte.
  static constexpr std::byte expected_event_packet_prefix{'\0'};

  util::const_byte_span portion;

  binsrv::event::reader_context context{};

  // if binlog is still open, there is no sense to close it and re-open
  // instead, we will just instruct this loop to process the
  // very first artificial rotate event in a special way
  bool skip_open_binlog{storage.is_binlog_open()};
  bool fetch_result{};

  while ((fetch_result = connection.fetch_binlog_event(portion)) &&
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
  if (fetch_result) {
    logger.log(binsrv::log_severity::info,
               "fetched everything and disconnected");
  } else {
    if (operation_mode == binsrv::operation_mode_type::fetch) {
      util::exception_location().raise<std::logic_error>(
          "fetch operation did not reach EOF reading binlog events");
    }
    logger.log(binsrv::log_severity::info,
               "timed out waiting for events and disconnected");
  }
}

} // anonymous namespace

int main(int argc, char *argv[]) {
  using namespace std::string_literals;

  int exit_code = EXIT_FAILURE;

  const auto cmd_args = util::to_command_line_agg_view(argc, argv);
  const auto number_of_cmd_args = std::size(cmd_args);
  const auto executable_name = util::extract_executable_name(cmd_args);

  binsrv::operation_mode_type operation_mode{
      binsrv::operation_mode_type::delimiter};
  auto cmd_args_validated{
      (number_of_cmd_args == binsrv::main_config::flattened_size + 2U ||
       number_of_cmd_args == 3U) &&
      boost::conversion::try_lexical_convert(cmd_args[1], operation_mode) &&
      operation_mode != binsrv::operation_mode_type::delimiter};

  if (!cmd_args_validated) {
    std::cerr << "usage: " << executable_name << " (fetch|pull))"
              << " <logger.level> <logger.file> <connection.host>"
                 " <connection.port> <connection.user> <connection.password>"
                 " <connect_timeout> <read_timeout> <write_timeout>"
                 " <storage.uri>\n"
              << "       " << executable_name
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

    if (operation_mode == binsrv::operation_mode_type::fetch) {
      logger->log(binsrv::log_severity::delimiter,
                  "'fetch' operation mode specified");
    } else if (operation_mode == binsrv::operation_mode_type::pull) {
      logger->log(binsrv::log_severity::delimiter,
                  "'pull' operation mode specified");
    } else {
      assert(false);
    }

    binsrv::main_config_ptr config;
    if (number_of_cmd_args == 3U) {
      logger->log(binsrv::log_severity::delimiter,
                  "reading connection configuration from the JSON file.");
      config = std::make_shared<binsrv::main_config>(cmd_args[2]);
    } else if (number_of_cmd_args == binsrv::main_config::flattened_size + 2U) {
      logger->log(binsrv::log_severity::delimiter,
                  "reading connection configuration from the command line "
                  "arguments.");
      config = std::make_shared<binsrv::main_config>(cmd_args.subspan(2U));
    } else {
      assert(false);
    }
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

    std::string msg;
    const auto &storage_config = config->root().get<"storage">();
    auto storage_backend{
        binsrv::storage_backend_factory::create(storage_config)};
    logger->log(binsrv::log_severity::info, "created binlog storage backend");
    msg = "description: ";
    msg += storage_backend->get_description();
    logger->log(binsrv::log_severity::info, msg);

    binsrv::storage storage{std::move(storage_backend)};
    logger->log(binsrv::log_severity::info,
                "created binlog storage with the provided backend");

    log_storage_info(*logger, storage);

    const auto &connection_config = config->root().get<"connection">();
    logger->log(binsrv::log_severity::info,
                "mysql connection string: " +
                    connection_config.get_connection_string());

    // TODO: put these parameters into configuration
    static constexpr std::uint32_t default_server_id{42U};
    static constexpr std::uint32_t default_idle_time_seconds{5U};

    const auto server_id{default_server_id};
    const auto idle_time_seconds{default_idle_time_seconds};

    const easymysql::library mysql_lib;
    logger->log(binsrv::log_severity::info, "initialized mysql client library");

    log_library_info(*logger, mysql_lib);

    receive_binlog_events(operation_mode, *logger, mysql_lib, connection_config,
                          server_id, storage);

    if (operation_mode == binsrv::operation_mode_type::pull) {
      std::size_t iteration_number{1U};
      while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(idle_time_seconds));

        logger->log(binsrv::log_severity::info,
                    "awoke after sleep and trying to reconnect (" +
                        std::to_string(iteration_number) + ")");
        receive_binlog_events(operation_mode, *logger, mysql_lib,
                              connection_config, server_id, storage);
        ++iteration_number;
      }
    }

    logger->log(binsrv::log_severity::info, "successfully shut down");
    exit_code = EXIT_SUCCESS;
  } catch (...) {
    handle_std_exception(logger);
  }

  return exit_code;
}
