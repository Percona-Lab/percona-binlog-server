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
#include <exception>
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

#include "app_version.hpp"

#include "binsrv/basic_logger.hpp"
#include "binsrv/ctime_timestamp.hpp"
#include "binsrv/exception_handling_helpers.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/logger_factory.hpp"
#include "binsrv/main_config.hpp"
#include "binsrv/operation_mode_type.hpp"
#include "binsrv/replication_mode_type.hpp"
#include "binsrv/size_unit.hpp"
#include "binsrv/storage.hpp"
// needed for storage_backend_type's operator <<
#include "binsrv/storage_backend_type.hpp" // IWYU pragma: keep
#include "binsrv/time_unit.hpp"

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "binsrv/models/error_response.hpp"
#include "binsrv/models/search_response.hpp"

#include "binsrv/events/checksum_algorithm_type.hpp"
#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_header_flag_type.hpp"
#include "binsrv/events/event.hpp"
#include "binsrv/events/event_view.hpp"
#include "binsrv/events/protocol_traits_fwd.hpp"
#include "binsrv/events/reader_context.hpp"

#include "easymysql/connection.hpp"
#include "easymysql/connection_config.hpp"
#include "easymysql/core_error.hpp"
#include "easymysql/library.hpp"
// Needed for ssl_mode_type's operator <<
#include "easymysql/ssl_mode_type.hpp" // IWYU pragma: keep

#include "util/byte_span_fwd.hpp"
#include "util/command_line_helpers.hpp"
#include "util/common_optional_types.hpp"
#include "util/ct_string.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/nv_tuple.hpp"
#include "util/semantic_version.hpp"

namespace {

[[nodiscard]] bool
check_cmd_args(const util::command_line_arg_view &cmd_args,
               binsrv::operation_mode_type &operation_mode,
               // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
               std::string_view &config_file_path,
               std::string_view &subcommand_value) noexcept {
  const auto number_of_cmd_args = std::size(cmd_args);

  static constexpr std::size_t expected_number_of_cmd_args_min{2U};

  if (number_of_cmd_args < expected_number_of_cmd_args_min) {
    return false;
  }

  operation_mode = binsrv::operation_mode_type::delimiter;

  if (!boost::conversion::try_lexical_convert(cmd_args[1], operation_mode)) {
    return false;
  }

  if (operation_mode == binsrv::operation_mode_type::delimiter) {
    return false;
  }

  static constexpr std::size_t expected_number_of_cmd_args_with_config{3U};
  static constexpr std::size_t
      expected_number_of_cmd_args_with_config_and_value{4U};

  switch (operation_mode) {
  case binsrv::operation_mode_type::fetch:
  case binsrv::operation_mode_type::pull:
    if (number_of_cmd_args != expected_number_of_cmd_args_with_config) {
      return false;
    }
    config_file_path = cmd_args[expected_number_of_cmd_args_with_config - 1U];
    return true;
  case binsrv::operation_mode_type::search_by_timestamp:
  case binsrv::operation_mode_type::search_by_gtid_set:
    if (number_of_cmd_args !=
        expected_number_of_cmd_args_with_config_and_value) {
      return false;
    }
    config_file_path = cmd_args[expected_number_of_cmd_args_with_config - 1U];
    subcommand_value =
        cmd_args[expected_number_of_cmd_args_with_config_and_value - 1U];
    return true;
  case binsrv::operation_mode_type::version:
    return number_of_cmd_args == expected_number_of_cmd_args_min;
    break;
  default:
    // should never be here
    assert(false);
    return false;
  }
}

template <typename T> util::optional_string to_log_string(const T &value) {
  return boost::lexical_cast<std::string>(value);
}

util::optional_string to_log_string(const binsrv::size_unit &value) {
  return value.get_description();
}

util::optional_string to_log_string(const binsrv::time_unit &value) {
  return value.get_description();
}

util::optional_string to_log_string(bool value) {
  return {value ? "true" : "false"};
}

template <typename T>
util::optional_string to_log_string(const std::optional<T> &value) {
  if (!value.has_value()) {
    return {};
  }
  return to_log_string(*value);
}

template <util::ct_string CTS, util::derived_from_named_value_tuple Config>
void log_config_param(binsrv::basic_logger &logger, const Config &config,
                      std::string_view label) {
  const auto opt_log_string{to_log_string(config.template get<CTS>())};
  if (opt_log_string.has_value()) {
    std::string msg{label};
    msg += ": ";
    msg += *opt_log_string;
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
    log_ssl_config_info(logger, *optional_ssl_config);
  }
  const auto &optional_tls_config{connection_config.get<"tls">()};
  if (optional_tls_config.has_value()) {
    log_tls_config_info(logger, *optional_tls_config);
  }
}

void log_rewrite_config_info(binsrv::basic_logger &logger,
                             const binsrv::rewrite_config &rewrite_config) {
  log_config_param<"base_file_name">(logger, rewrite_config,
                                     "rewrite base binlog file name");
  log_config_param<"file_size">(logger, rewrite_config,
                                "rewrite binlog file size");
}
void log_replication_config_info(
    binsrv::basic_logger &logger,
    const binsrv::replication_config &replication_config) {

  log_config_param<"server_id">(logger, replication_config,
                                "mysql replication server id");
  log_config_param<"idle_time">(logger, replication_config,
                                "mysql replication idle time (seconds)");
  log_config_param<"verify_checksum">(
      logger, replication_config, "mysql replication checksum verification");
  log_config_param<"mode">(logger, replication_config,
                           "mysql replication mode");
  const auto &optional_rewrite_config{replication_config.get<"rewrite">()};
  if (optional_rewrite_config.has_value()) {
    log_rewrite_config_info(logger, *optional_rewrite_config);
  }
}

void log_storage_config_info(binsrv::basic_logger &logger,
                             const binsrv::storage_config &storage_config) {

  log_config_param<"backend">(logger, storage_config,
                              "binlog storage backend type");
  // not printing "uri" here deliberately to avoid credentials leaking
  log_config_param<"fs_buffer_directory">(
      logger, storage_config,
      "binlog storage backend filesystem buffer directory");
  log_config_param<"checkpoint_size">(
      logger, storage_config, "binlog storage backend checkpointing size");
  log_config_param<"checkpoint_interval">(
      logger, storage_config, "binlog storage backend checkpointing interval");
}

void log_storage_info(binsrv::basic_logger &logger,
                      const binsrv::storage &storage) {
  std::string msg{"created binlog storage with the following backend: "};
  msg += storage.get_backend_description();
  logger.log(binsrv::log_severity::info, msg);

  msg.clear();
  msg = "binlog storage initialized in ";
  msg += boost::lexical_cast<std::string>(storage.get_replication_mode());
  msg += " mode";
  logger.log(binsrv::log_severity::info, msg);

  msg.clear();
  if (storage.is_empty()) {
    msg = "binlog storage initialized on an empty directory";
  } else {
    msg = "binlog storage initialized at \"";
    msg += storage.get_current_binlog_name().str();
    msg += "\":";
    msg += std::to_string(storage.get_current_position());
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
    const binsrv::storage &storage, bool verify_checksum,
    easymysql::connection_replication_mode_type blocking_mode) {
  const auto replication_mode{storage.get_replication_mode()};

  std::string msg{"switched to replication (checksum "};
  msg += (verify_checksum ? "enabled" : "disabled");
  msg += ", ";
  msg += boost::lexical_cast<std::string>(replication_mode);
  msg += +" mode)";
  logger.log(binsrv::log_severity::info, msg);

  msg = "replication info (server id ";
  msg += std::to_string(server_id);
  msg += ", ";
  msg += (blocking_mode == easymysql::connection_replication_mode_type::blocking
              ? "blocking"
              : "non-blocking");
  msg += ", starting from ";
  if (replication_mode == binsrv::replication_mode_type::position) {
    if (storage.is_empty()) {
      msg += "the very beginning";
    } else {
      msg += storage.get_current_binlog_name().str();
      msg += ":";
      msg += std::to_string(storage.get_current_position());
    }
  } else {
    const auto &gtids{storage.get_gtids()};
    if (gtids.is_empty()) {
      msg += "an empty";
    } else {
      msg += "the ";
      msg += boost::lexical_cast<std::string>(gtids);
    }
    msg += " GTID set";
  }
  msg += ")";
  logger.log(binsrv::log_severity::info, msg);
}

void log_span_dump(binsrv::basic_logger &logger,
                   util::const_byte_span portion) {
  logger.log(binsrv::log_severity::debug,
             "fetched " + std::to_string(std::size(portion)) +
                 "-byte(s) event from binlog");
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

void process_artificial_rotate_event(
    const binsrv::events::event_view &current_event_v,
    binsrv::basic_logger &logger, binsrv::storage &storage) {
  assert(current_event_v.get_common_header_view().get_type_code() ==
         binsrv::events::code_type::rotate);
  assert(current_event_v.get_common_header_view().get_flags().has_element(
      binsrv::events::common_header_flag_type::artificial));

  const binsrv::events::generic_body<binsrv::events::code_type::rotate>
      current_rotate_body{current_event_v.get_body_raw()};

  bool binlog_opening_needed{true};

  if (storage.is_binlog_open()) {
    // here we take a "shortcut" path - upon losing connection to the MySQL
    // server, we do not close storage's binlog file immediately expecting
    // that upon reconnection we will be able to continue writing to the
    // same file

    // so, here we just need to make sure that (binlog name, position) pair
    // in the artificial ROTATE event matches the current storage state

    // also, in case when the server was not shut down properly, it won't
    // have ROTATE or STOP event as the last one in the binlog, so here we
    // handle this case by closing the old binlog and opening a new one

    if (current_rotate_body.get_parsed_binlog() ==
        storage.get_current_binlog_name()) {
      // in addition, in position-based replication mode we also need to check
      // the position
      const binsrv::events::generic_post_header<
          binsrv::events::code_type::rotate>
          current_rotate_post_header{current_event_v.get_post_header_raw()};

      if (current_rotate_post_header.get_position_raw() !=
          storage.get_current_position()) {
        util::exception_location().raise<std::logic_error>(
            "unexpected binlog position in artificial rotate event");
      }

      binlog_opening_needed = false;

      const std::string current_binlog_name{
          storage.get_current_binlog_name().str()};
      logger.log(binsrv::log_severity::info,
                 "storage: reused already open binlog file: " +
                     current_binlog_name);

    } else {
      // if names do not match, we need to close the currently open
      // binlog and make sure that binlog_opening_needed is set to true, so
      // that we will open a new one later
      const std::string old_binlog_name{
          storage.get_current_binlog_name().str()};
      storage.close_binlog();
      logger.log(binsrv::log_severity::info,
                 "storage: closed binlog file left open: " + old_binlog_name);
      // binlog_opening_needed remains true in this branch
      assert(binlog_opening_needed);
    }
  }
  if (binlog_opening_needed) {
    const auto binlog_open_result{
        storage.open_binlog(current_rotate_body.get_parsed_binlog())};

    std::string message{"storage: "};
    if (binlog_open_result == binsrv::open_binlog_status::created) {
      message += "created a new";
    } else {
      message += "opened an existing";
      if (binlog_open_result == binsrv::open_binlog_status::opened_empty) {
        message += " (empty)";
      } else if (binlog_open_result ==
                 binsrv::open_binlog_status::opened_at_magic_payload_offset) {
        message += " (with magic payload only)";
      }
    }
    message += " binlog file: ";
    message += current_rotate_body.get_readable_binlog();
    logger.log(binsrv::log_severity::info, message);
  }
}

void process_rotate_or_stop_event(binsrv::basic_logger &logger,
                                  binsrv::storage &storage) {
  const std::string old_binlog_name{storage.get_current_binlog_name().str()};
  storage.close_binlog();
  logger.log(binsrv::log_severity::info,
             "storage: closed binlog file: " + old_binlog_name);
}

void process_binlog_event(const binsrv::events::event_view &current_event_v,
                          binsrv::basic_logger &logger,
                          binsrv::events::reader_context &context,
                          binsrv::storage &storage) {
  const auto current_common_header_v{current_event_v.get_common_header_view()};
  const auto readable_flags{current_common_header_v.get_readable_flags()};
  logger.log(binsrv::log_severity::info,
             "event  : " +
                 std::string{current_common_header_v.get_readable_type_code()} +
                 (readable_flags.empty() ? "" : " (" + readable_flags + ")"));
  logger.log(binsrv::log_severity::debug,
             "event  : [parsed view] " +
                 boost::lexical_cast<std::string>(current_event_v));

  const bool info_only{context.process_event_view(current_event_v)};

  if (info_only) {
    logger.log(
        binsrv::log_severity::info,
        "event  : [info_only] - will not be written to the binary log file");
  }

  if (context.is_at_transaction_boundary()) {
    logger.log(
        binsrv::log_severity::info,
        "event  : [end_of_transaction] " +
            boost::lexical_cast<std::string>(context.get_transaction_gtid()));
  }

  // here we additionally check for log level because event materialization
  // is not a trivial operation
  if (binsrv::log_severity::debug >= logger.get_min_level()) {
    const binsrv::events::event current_event{current_event_v};
    logger.log(binsrv::log_severity::debug,
               "event  : [parsed] " +
                   boost::lexical_cast<std::string>(current_event));
  }

  const auto code = current_common_header_v.get_type_code();
  const auto is_artificial{current_common_header_v.get_flags().has_element(
      binsrv::events::common_header_flag_type::artificial)};

  // processing the very first event in the sequence - artificial ROTATE event
  if (code == binsrv::events::code_type::rotate && is_artificial) {
    process_artificial_rotate_event(current_event_v, logger, storage);
  }

  // checking if the event needs to be written to the binlog
  if (!info_only) {
    storage.write_event(current_event_v.get_portion(),
                        context.is_at_transaction_boundary(),
                        context.get_transaction_gtid(),
                        current_common_header_v.get_timestamp());
  }

  // processing the very last event in the sequence - either a non-artificial
  // ROTATE event or a STOP event
  if ((code == binsrv::events::code_type::rotate && !is_artificial) ||
      code == binsrv::events::code_type::stop) {
    process_rotate_or_stop_event(logger, storage);
  }
}

[[nodiscard]] binsrv::events::event_view
generate_rotate_event(binsrv::events::event_storage &event_buffer,
                      const binsrv::events::reader_context &context,
                      std::uint32_t offset, bool current_timestamp,
                      std::uint32_t server_id, bool artificial,
                      const binsrv::composite_binlog_name &binlog_name) {
  const binsrv::events::generic_post_header<binsrv::events::code_type::rotate>
      post_header{binsrv::events::magic_binlog_offset};
  const binsrv::events::generic_body<binsrv::events::code_type::rotate> body{
      binlog_name};

  binsrv::ctime_timestamp timestamp{};
  if (current_timestamp) {
    timestamp = binsrv::ctime_timestamp::now();
  }

  binsrv::events::common_header_flag_set flags{};
  if (artificial) {
    flags |= binsrv::events::common_header_flag_type::artificial;
  }

  // the value of the 'include_checksum' parameters is taken from the
  // 'reader_context': immediately after reconnection it will be equal to
  // the '<replication.verify_checksum>' configuration parameter and after
  // that will be taken from the FORMAT_DESCRIPTION events, which in the
  // rewrite mode will be generated by us and therefore will always include
  // 'checksum_algorithm' set to 'crc32'
  const auto generated_event{
      binsrv::events::event::create_event<binsrv::events::code_type::rotate>(
          offset, timestamp, server_id, flags, post_header, body,
          context.is_footer_expected(), event_buffer)};

  return binsrv::events::event_view{context,
                                    util::const_byte_span{event_buffer}};
}

[[nodiscard]] binsrv::events::event_view
generate_format_description_event(binsrv::events::event_storage &event_buffer,
                                  const binsrv::events::reader_context &context,
                                  std::uint32_t offset,
                                  std::uint32_t server_id) {
  const util::semantic_version server_version{
      context.get_current_encoded_server_version()};
  const binsrv::events::generic_post_header<
      binsrv::events::code_type::format_description>
      post_header{
          binsrv::events::default_binlog_version, server_version,
          binsrv::ctime_timestamp::now(),
          binsrv::events::default_common_header_length,
          binsrv::events::reader_context::get_hardcoded_post_header_lengths(
              server_version.get_encoded())};
  const binsrv::events::generic_body<
      binsrv::events::code_type::format_description>
      body{binsrv::events::checksum_algorithm_type::crc32};

  // enforcing checksums for all rewritten upcoming events
  const auto generated_event{binsrv::events::event::create_event<
      binsrv::events::code_type::format_description>(
      offset, binsrv::ctime_timestamp::now(), server_id,
      binsrv::events::common_header_flag_set{}, post_header, body,
      true /* include_checksum */, event_buffer)};

  return binsrv::events::event_view{context,
                                    util::const_byte_span{event_buffer}};
}

[[nodiscard]] binsrv::events::event_view
generate_previous_gtids_log_event(binsrv::events::event_storage &event_buffer,
                                  const binsrv::events::reader_context &context,
                                  std::uint32_t offset, std::uint32_t server_id,
                                  const binsrv::gtids::gtid_set &gtids) {
  const binsrv::events::generic_post_header<
      binsrv::events::code_type::previous_gtids_log>
      post_header{};
  const binsrv::events::generic_body<
      binsrv::events::code_type::previous_gtids_log>
      body{gtids};
  const auto generated_previous_gtids_log_event{
      binsrv::events::event::create_event<
          binsrv::events::code_type::previous_gtids_log>(
          offset, binsrv::ctime_timestamp::now(), server_id,
          binsrv::events::common_header_flag_set{}, post_header, body, true,
          event_buffer)};

  return binsrv::events::event_view{context,
                                    util::const_byte_span{event_buffer}};
}

void rewrite_and_process_binlog_event(
    const binsrv::events::event_view &current_event_v,
    binsrv::basic_logger &logger, binsrv::events::reader_context &context,
    binsrv::storage &storage, std::uint32_t server_id,
    std::string_view base_file_name, std::uint64_t file_size) {
  const auto current_common_header_v = current_event_v.get_common_header_view();
  const auto code = current_common_header_v.get_type_code();

  // for ROTATE (both artificial and non-artificial), FORMAT_DESCRIPTION,
  // PREVIOUS_GTIDS_LOG, and STOP events we don't have to do anything -
  // simply return early from this function
  if (code == binsrv::events::code_type::format_description ||
      code == binsrv::events::code_type::previous_gtids_log ||
      code == binsrv::events::code_type::rotate ||
      code == binsrv::events::code_type::stop) {
    const auto readable_flags{current_common_header_v.get_readable_flags()};
    logger.log(
        binsrv::log_severity::info,
        "rewrite: encountered " +
            std::string{current_common_header_v.get_readable_type_code()} +
            (readable_flags.empty() ? "" : " (" + readable_flags + ")") +
            " event in the rewrite mode - skipping");
    return;
  }

  // the very first step is to check if we need to close the old binary log
  // file and open a new one in case when we reached the file size specified
  // in the 'rewrite_config' or this is the very first event we are going to
  // write to an empty storage

  // in case of an empty storage we need to generate the following:
  // 1. ROTATE(artificial    ) <rewrite.base_file_name>.000001:4
  // 2. FORMAT_DESCRIPTION
  // 3. PREVIOUS_GTIDS_LOG

  // in case when the storage is not empty, we are at transaction boundary,
  // and current binlog file reached the file size specified in the
  // 'rewrite_config', we need to generate the following:
  // 0. ROTATE(non-artificial) <rewrite.base_file_name>.<index + 1>:4
  // 1. ROTATE(artificial    ) <rewrite.base_file_name>.<index + 1>:4
  // 2. FORMAT_DESCRIPTION
  // 3. PREVIOUS_GTIDS_LOG

  if (context.is_fresh() || (context.is_at_transaction_boundary() &&
                             storage.get_current_position() >= file_size)) {
    binsrv::events::event_storage event_buffer;
    std::uint32_t offset{0U};

    // generating next binlog file name based on base file name from the
    // configuration file <rewrite.base_file_name> and current binlog file
    // sequence number from the storage

    // please notice that if storage is empty, then the sequence number will be
    // zero
    binsrv::composite_binlog_name binlog_name{};
    if (storage.is_empty()) {
      // the very first time we receive an event on an empty storage
      binlog_name = binsrv::composite_binlog_name{base_file_name, 1U};
    } else if (context.is_fresh()) {
      // this is the very first event we received after reconnection
      // (the storage is not empty and we have an active binlog in it)
      binlog_name = storage.get_current_binlog_name();
    } else {
      // we are at transaction boundary and reached max binlog file size
      binlog_name = storage.get_current_binlog_name().next();
    }

    if (!context.is_fresh()) {
      // generate and process ROTATE(non-artificial) event
      offset = static_cast<std::uint32_t>(storage.get_current_position());
      const auto generated_rotate_event_v{generate_rotate_event(
          event_buffer, context, offset, true /* current timestamp */,
          server_id, false /* non-artificial */, binlog_name)};
      logger.log(binsrv::log_severity::info,
                 "rewrite: generated rotate event in the rewrite mode");
      process_binlog_event(generated_rotate_event_v, logger, context, storage);
    }

    // generate and process ROTATE(artificial) event
    offset = 0U;
    // artificial ROTATE event must include zero timestamp
    const auto generated_artificial_rotate_event_v{generate_rotate_event(
        event_buffer, context, offset, false /* zero timestamp */, server_id,
        true /* artificial */, binlog_name)};
    logger.log(
        binsrv::log_severity::info,
        "rewrite: generated artificial rotate event in the rewrite mode");
    process_binlog_event(generated_artificial_rotate_event_v, logger, context,
                         storage);

    // generate and process FORMAT_DESCRIPTION event
    offset = binsrv::events::magic_binlog_offset;
    const auto generated_format_description_event_v{
        generate_format_description_event(event_buffer, context, offset,
                                          server_id)};
    logger.log(
        binsrv::log_severity::info,
        "rewrite: generated format description event in the rewrite mode");
    process_binlog_event(generated_format_description_event_v, logger, context,
                         storage);

    // generate and process PREVIOUS_GTIDS_LOG event
    offset += static_cast<std::uint32_t>(
        generated_format_description_event_v.get_total_size());
    const auto generated_previous_gtids_log_event_v{
        generate_previous_gtids_log_event(event_buffer, context, offset,
                                          server_id, storage.get_gtids())};
    logger.log(
        binsrv::log_severity::info,
        "rewrite: generated previous gtids log event in the rewrite mode");
    process_binlog_event(generated_previous_gtids_log_event_v, logger, context,
                         storage);
  }

  // in rewrite mode we need to update next_event_position (and optional
  // checksum in the footer) in the received event data portion
  binsrv::events::event_storage buffer{};
  const auto event_copy_uv{binsrv::events::materialize(
      current_event_v, buffer,
      binsrv::events::materialization_type::force_add_checksum)};
  {
    // TODO: optimize redundant checksum recalculation
    const auto proxy{event_copy_uv.get_write_proxy()};
    proxy.get_common_header_updatable_view().set_next_event_position_raw(
        static_cast<std::uint32_t>(storage.get_current_position() +
                                   event_copy_uv.get_total_size()));
  }
  process_binlog_event(event_copy_uv, logger, context, storage);
}

bool open_connection_and_switch_to_replication(
    binsrv::operation_mode_type operation_mode, binsrv::basic_logger &logger,
    const easymysql::library &mysql_lib,
    const easymysql::connection_config &connection_config,
    std::uint32_t server_id, bool verify_checksum, binsrv::storage &storage,
    easymysql::connection &connection) {
  try {
    connection = mysql_lib.create_connection(connection_config);
  } catch (const easymysql::core_error &) {
    if (operation_mode == binsrv::operation_mode_type::fetch) {
      throw;
    }
    logger.log(binsrv::log_severity::info,
               "unable to establish connection to mysql server");
    return false;
  }

  logger.log(binsrv::log_severity::info,
             "established connection to mysql server");

  log_connection_info(logger, connection);

  const auto blocking_mode{
      operation_mode == binsrv::operation_mode_type::fetch
          ? easymysql::connection_replication_mode_type::non_blocking
          : easymysql::connection_replication_mode_type::blocking};

  try {
    if (storage.is_in_gtid_replication_mode()) {
      const auto &gtids{storage.get_gtids()};
      const auto encoded_size{gtids.calculate_encoded_size()};

      binsrv::gtids::gtid_set_storage encoded_gtids_buffer(encoded_size);
      util::byte_span destination{encoded_gtids_buffer};
      gtids.encode_to(destination);

      connection.switch_to_gtid_replication(
          server_id, util::const_byte_span{encoded_gtids_buffer},
          verify_checksum, blocking_mode);
    } else {
      if (storage.is_empty()) {
        connection.switch_to_position_replication(server_id, verify_checksum,
                                                  blocking_mode);
      } else {
        connection.switch_to_position_replication(
            server_id, storage.get_current_binlog_name().str(),
            storage.get_current_position(), verify_checksum, blocking_mode);
      }
    }
  } catch (const easymysql::core_error &) {
    if (operation_mode == binsrv::operation_mode_type::fetch) {
      throw;
    }
    logger.log(binsrv::log_severity::info, "unable to switch to replication");
    return false;
  }

  log_replication_info(logger, server_id, storage, verify_checksum,
                       blocking_mode);
  return true;
}

void receive_binlog_events(
    binsrv::operation_mode_type operation_mode,
    const volatile std::atomic_flag &termination_flag,
    binsrv::basic_logger &logger, const easymysql::library &mysql_lib,
    const easymysql::connection_config &connection_config,
    std::uint32_t server_id, bool verify_checksum, binsrv::storage &storage,
    const binsrv::optional_rewrite_config &optional_rewrite_config) {
  easymysql::connection connection{};
  if (!open_connection_and_switch_to_replication(
          operation_mode, logger, mysql_lib, connection_config, server_id,
          verify_checksum, storage, connection)) {
    return;
  }

  // Network streams are requested with COM_BINLOG_DUMP and
  // each Binlog Event response is prepended with 00 OK-byte.
  static constexpr std::byte expected_event_packet_prefix{'\0'};

  util::const_byte_span portion;

  binsrv::events::reader_context context{
      connection.get_server_version(), verify_checksum,
      storage.get_replication_mode(), storage.get_current_binlog_name().str(),
      static_cast<std::uint32_t>(storage.get_current_position())};

  bool fetch_result{};

  while (!termination_flag.test() &&
         (fetch_result = connection.fetch_binlog_event(portion)) &&
         !portion.empty()) {
    if (portion[0] != expected_event_packet_prefix) {
      util::exception_location().raise<std::runtime_error>(
          "unexpected event prefix");
    }
    portion = portion.subspan(1U);
    log_span_dump(logger, portion);

    const binsrv::events::event_view current_event_v{context, portion};

    if (optional_rewrite_config.has_value()) {
      // in rewrite mode we need to ignore ROTATE (artificial),
      // FORMAT_DESCRIPTION, PREVIOUS_GTIDS_LOG, ROTATE (non-artificial),
      // and STOP events
      rewrite_and_process_binlog_event(
          current_event_v, logger, context, storage, server_id,
          optional_rewrite_config->get<"base_file_name">(),
          optional_rewrite_config->get<"file_size">().get_value());
    } else {
      process_binlog_event(current_event_v, logger, context, storage);
    }
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

  // in GTID-based replication mode we also need to discard some data in the
  // transaction event buffer to make sure that upon reconnection we will
  // continue operation from the transaction boundary

  // in position-based replication mode this is not needed as it is not a
  // problem to resume streaming from a position that does not correspond to
  // transaction boundary
  if (storage.is_in_gtid_replication_mode()) {
    storage.discard_incomplete_transaction_events();
  }

  // connection termination is a good place to flush any remaining data
  // in the event buffer - this can be considered the third kind of
  // checkpointing (in addition to size-based and time-based ones)
  storage.flush_event_buffer();

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

bool handle_version() {
  std::cout << app_version.get_string() << '\n';
  return true;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool handle_search_by_timestamp(std::string_view config_file_path,
                                std::string_view subcommand_value) {
  bool operation_successful{false};
  std::string result;

  try {
    binsrv::ctime_timestamp timestamp;
    if (!binsrv::ctime_timestamp::try_parse(subcommand_value, timestamp)) {
      throw std::runtime_error("Invalid timestamp format");
    }

    const binsrv::main_config config{config_file_path};
    const auto &storage_config = config.root().get<"storage">();
    const auto &replication_config = config.root().get<"replication">();
    const auto replication_mode{replication_config.get<"mode">()};

    const binsrv::storage storage{
        storage_config, binsrv::storage_construction_mode_type::querying_only,
        replication_mode};

    binsrv::models::search_response response;
    const auto &binlog_records{storage.get_binlog_records()};
    if (binlog_records.empty()) {
      throw std::runtime_error("Binlog storage is empty");
    }
    for (const auto &record : binlog_records) {
      // break when we find a binlog file with min timestamp greater
      // than the provided one
      if (record.timestamps.get_min_timestamp() > timestamp) {
        break;
      }
      response.add_record(record.name.str(), record.size,
                          storage.get_binlog_uri(record.name),
                          record.previous_gtids, record.added_gtids,
                          record.timestamps.get_min_timestamp().get_value(),
                          record.timestamps.get_max_timestamp().get_value());
    }
    if (response.root().get<"result">().empty()) {
      throw std::runtime_error("Timestamp is too old");
    }
    result = response.str();
    operation_successful = true;
  } catch (const std::exception &e) {
    const binsrv::models::error_response response{e.what()};
    result = response.str();
  }
  std::cout << result << '\n';
  return operation_successful;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool handle_search_by_gtid_set(std::string_view config_file_path,
                               std::string_view subcommand_value) {
  bool operation_successful{false};
  std::string result;

  try {
    binsrv::gtids::gtid_set remaining_gtids{subcommand_value};

    const binsrv::main_config config{config_file_path};
    const auto &storage_config = config.root().get<"storage">();
    const auto &replication_config = config.root().get<"replication">();
    const auto replication_mode{replication_config.get<"mode">()};

    const binsrv::storage storage{
        storage_config, binsrv::storage_construction_mode_type::querying_only,
        replication_mode};

    const auto &binlog_records{storage.get_binlog_records()};
    if (binlog_records.empty()) {
      throw std::runtime_error("Binlog storage is empty");
    }

    binsrv::models::search_response response;
    if (!storage.is_in_gtid_replication_mode()) {
      throw std::runtime_error("GTID set search is not supported in storages "
                               "created in position-based replication mode");
    }

    for (const auto &record : binlog_records) {
      if (remaining_gtids.is_empty()) {
        break;
      }
      if (!record.added_gtids.has_value()) {
        continue;
      }
      if (!binsrv::gtids::intersects(remaining_gtids, *record.added_gtids)) {
        continue;
      }
      remaining_gtids.subtract(*record.added_gtids);

      response.add_record(record.name.str(), record.size,
                          storage.get_binlog_uri(record.name),
                          record.previous_gtids, record.added_gtids,
                          record.timestamps.get_min_timestamp().get_value(),
                          record.timestamps.get_max_timestamp().get_value());
    }
    if (!remaining_gtids.is_empty()) {
      throw std::runtime_error("The specified GTID set cannot be covered");
    }
    result = response.str();
    operation_successful = true;
  } catch (const std::exception &e) {
    const binsrv::models::error_response response{e.what()};
    result = response.str();
  }
  std::cout << result << '\n';
  return operation_successful;
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

  const auto cmd_args = util::to_command_line_agg_view(argc, argv);
  const auto executable_name = util::extract_executable_name(cmd_args);

  binsrv::operation_mode_type operation_mode{
      binsrv::operation_mode_type::delimiter};
  std::string_view config_file_path;
  std::string_view subcommand_value;
  const auto cmd_args_checked{check_cmd_args(
      cmd_args, operation_mode, config_file_path, subcommand_value)};
  if (!cmd_args_checked) {
    std::cerr << "usage: " << executable_name
              << " (fetch|pull)) <json_config_file>\n"
              << "       " << executable_name
              << " search_by_timestamp <json_config_file> <timestamp>\n"
              << "       " << executable_name
              << " search_by_gtid_set <json_config_file> <gtid_set>\n"
              << "       " << executable_name << " version\n";
    return EXIT_FAILURE;
  }

  // handling the 'version' command
  if (operation_mode == binsrv::operation_mode_type::version) {
    return handle_version() ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  // handling the 'search_by_timestamp' command
  if (operation_mode == binsrv::operation_mode_type::search_by_timestamp) {
    return handle_search_by_timestamp(config_file_path, subcommand_value)
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
  }

  // handling the 'search_by_gtid_set' command
  if (operation_mode == binsrv::operation_mode_type::search_by_gtid_set) {
    return handle_search_by_gtid_set(config_file_path, subcommand_value)
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
  }

  int exit_code = EXIT_FAILURE;

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

    logger->log(binsrv::log_severity::delimiter,
                "reading configuration from the JSON file.");
    const binsrv::main_config config{config_file_path};

    const auto &logger_config = config.root().get<"logger">();
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

    logger->log(binsrv::log_severity::delimiter,
                "application version: " + app_version.get_string());

    assert(operation_mode == binsrv::operation_mode_type::fetch ||
           operation_mode == binsrv::operation_mode_type::pull);
    std::string msg;
    msg = '\'';
    msg += boost::lexical_cast<std::string>(operation_mode);
    msg += "' operation mode specified";
    logger->log(binsrv::log_severity::delimiter, msg);

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

    const auto &storage_config = config.root().get<"storage">();
    log_storage_config_info(*logger, storage_config);

    const auto &connection_config = config.root().get<"connection">();
    log_connection_config_info(*logger, connection_config);

    const auto &replication_config = config.root().get<"replication">();
    log_replication_config_info(*logger, replication_config);

    const auto server_id{replication_config.get<"server_id">()};
    const auto idle_time_seconds{replication_config.get<"idle_time">()};
    const auto verify_checksum{replication_config.get<"verify_checksum">()};
    const auto replication_mode{replication_config.get<"mode">()};
    const auto optional_rewrite_config{replication_config.get<"rewrite">()};

    binsrv::storage storage{storage_config,
                            binsrv::storage_construction_mode_type::streaming,
                            replication_mode};
    log_storage_info(*logger, storage);

    const easymysql::library mysql_lib;
    logger->log(binsrv::log_severity::info, "initialized mysql client library");

    log_library_info(*logger, mysql_lib);

    receive_binlog_events(operation_mode, termination_flag, *logger, mysql_lib,
                          connection_config, server_id, verify_checksum,
                          storage, optional_rewrite_config);

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
                              verify_checksum, storage,
                              optional_rewrite_config);
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
