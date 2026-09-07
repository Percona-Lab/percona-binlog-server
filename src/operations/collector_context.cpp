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

#include "operations/collector_context.hpp"

#include "app_version.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

#include <boost/lexical_cast.hpp>

#include <boost/asio/io_context.hpp>

#include "binsrv/basic_logger.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/logger_factory.hpp"
#include "binsrv/main_config.hpp"
#include "binsrv/replication_mode_type.hpp"
#include "binsrv/storage.hpp"

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_header_flag_type.hpp"
#include "binsrv/events/event.hpp"
#include "binsrv/events/event_view.hpp"
#include "binsrv/events/protocol_traits_fwd.hpp"
#include "binsrv/events/reader_context.hpp"
#include "binsrv/events/rewriter.hpp"

#include "binsrv/gtids/common_types.hpp"

#include "easymysql/connection.hpp"
#include "easymysql/core_error.hpp"
#include "easymysql/library.hpp"

#include "operations/event_generation_helpers.hpp"
#include "operations/logger_helpers.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/command_line_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace operations {

collector_context::collector_context(
    easymysql::connection_replication_mode_type connection_replication_mode,
    binsrv::main_config_ptr config, binsrv::basic_logger_ptr logger)
    : connection_replication_mode_{connection_replication_mode},
      config_{std::move(config)}, logger_{std::move(logger)} {
  const auto &keyring_config{config_->root().get<"keyring">()};
  if (keyring_config.has_value()) {
    log_keyring_config_info(*logger_, *keyring_config);
  } else {
    logger_->log(binsrv::log_severity::info,
                 "keyring configuration options are not specified");
  }

  const auto &storage_config{config_->root().get<"storage">()};
  log_storage_config_info(*logger_, storage_config);

  const auto &connection_config{config_->root().get<"connection">()};
  log_connection_config_info(*logger_, connection_config);

  const auto &replication_config{config_->root().get<"replication">()};
  log_replication_config_info(*logger_, replication_config);

  const auto replication_mode{replication_config.get<"mode">()};

  storage_ = std::make_unique<binsrv::storage>(
      logger_, keyring_config, storage_config,
      binsrv::storage_construction_mode_type::streaming, replication_mode);
  log_storage_info(*logger_, *storage_);

  mysql_lib_ = std::make_unique<easymysql::library>();
  logger_->log(binsrv::log_severity::info, "initialized mysql client library");

  log_library_info(*logger_, *mysql_lib_);
}

collector_context::~collector_context() = default;

[[nodiscard]] binsrv::basic_logger_ptr
collector_context::initialize_console_logger(
    util::command_line_arg_view cmd_args) {
  static constexpr auto default_log_level = binsrv::log_severity::trace;

  const binsrv::logger_config initial_logger_config{
      {{default_log_level}, {""}}};

  binsrv::basic_logger_ptr logger{
      binsrv::logger_factory::create(initial_logger_config)};
  // logging with "delimiter" level has the highest priority and empty label
  const auto executable_name{util::extract_executable_name(cmd_args)};
  logger->log(binsrv::log_severity::delimiter,
              '"' + executable_name + '"' +
                  " started with the following command line arguments:");
  logger->log(binsrv::log_severity::delimiter,
              util::get_readable_command_line_arguments(cmd_args));

  logger->log(binsrv::log_severity::delimiter,
              "reading configuration from the JSON file.");

  return logger;
}

void collector_context::reinitialize_logger_from_config(
    binsrv::basic_logger_ptr &logger, const binsrv::main_config &config) {
  const auto &logger_config{config.root().get<"logger">()};
  if (!logger_config.has_file()) {
    logger->set_min_level(logger_config.get<"level">());
  } else {
    logger->log(binsrv::log_severity::delimiter,
                "redirecting logging to \"" + logger_config.get<"file">() +
                    "\"");
    auto new_logger = binsrv::logger_factory::create(logger_config);
    std::swap(logger, new_logger);
  }

  const auto log_level_label = binsrv::to_string_view(logger->get_min_level());
  logger->log(binsrv::log_severity::delimiter,
              "logging level set to \"" + std::string{log_level_label} + '"');

  logger->log(binsrv::log_severity::delimiter,
              "application version: " + app_version.get_string());
}

bool collector_context::receive_binlog_events(
    const boost::asio::io_context &io_ctx) {
  const auto &replication_config{config_->root().get<"replication">()};

  const auto verify_checksum{replication_config.get<"verify_checksum">()};

  easymysql::connection connection{};
  if (!open_connection_and_switch_to_replication(connection)) {
    return false;
  }

  // Network streams are requested with COM_BINLOG_DUMP and
  // each Binlog Event response is prepended with 00 OK-byte.
  static constexpr std::byte expected_event_packet_prefix{'\0'};

  util::const_byte_span portion;

  binsrv::events::reader_context context{
      connection.get_server_version(), verify_checksum,
      storage_->get_replication_mode(),
      storage_->get_current_binlog_name().str(),
      static_cast<std::uint32_t>(storage_->get_current_position())};

  bool fetch_result{};

  const auto &optional_rewrite_config{replication_config.get<"rewrite">()};
  while (!io_ctx.stopped() &&
         (fetch_result = connection.fetch_binlog_event(portion)) &&
         !portion.empty()) {
    if (portion[0] != expected_event_packet_prefix) {
      util::exception_location().raise<std::runtime_error>(
          "unexpected event prefix");
    }
    portion = portion.subspan(1U);
    log_span_dump(*logger_, portion);

    const binsrv::events::event_view current_event_v{context, portion};

    if (optional_rewrite_config.has_value()) {
      // in rewrite mode we need to ignore ROTATE (artificial),
      // FORMAT_DESCRIPTION, PREVIOUS_GTIDS_LOG, ROTATE (non-artificial),
      // and STOP events
      rewrite_and_process_binlog_event(current_event_v, context);
    } else {
      process_binlog_event(current_event_v, context);
    }
  }
  // the loop above can be terminated in three ways:
  // 1) termination signal was received
  //    (termination_flag_->is_flag_set() is true)
  // 2) connection timed out waiting for events (fetch_result is false)
  // 3) fetched everything and disconnected (fetch_result is true)
  //    (can only happen in "fetch", non-blocking node)
  if (io_ctx.stopped()) {
    // case (1)
    logger_->log(binsrv::log_severity::info,
                 "fetching binlog events loop terminated by signal");
    return false;
  }
  if (fetch_result) {
    // case (3)
    return true;
  }

  // case (2)

  // Truncate the in-memory event buffer to the last completed transaction so
  // the persisted stream offset matches a transaction boundary. On reconnect,
  // reader_context always expects the first logical event after the
  // pseudo-preamble to be anonymous_gtid_log / gtid_log / gtid_tagged_log
  storage_->discard_incomplete_transaction_events();

  // connection termination is a good place to flush any remaining data
  // in the event buffer - this can be considered the third kind of
  // checkpointing (in addition to size-based and time-based ones)
  storage_->flush_event_buffer();

  logger_->log(binsrv::log_severity::info,
               "timed out waiting for events and disconnected");
  return false;
}

bool collector_context::open_connection_and_switch_to_replication(
    easymysql::connection &connection) {
  try {
    connection =
        mysql_lib_->create_connection(config_->root().get<"connection">());
  } catch (const easymysql::core_error &) {
    logger_->log(binsrv::log_severity::error,
                 "unable to establish connection to mysql server");
    return false;
  }

  logger_->log(binsrv::log_severity::info,
               "established connection to mysql server");

  log_connection_info(*logger_, connection);

  const auto &replication_config{config_->root().get<"replication">()};

  const auto server_id{replication_config.get<"server_id">()};
  const auto verify_checksum{replication_config.get<"verify_checksum">()};
  try {
    if (storage_->is_in_gtid_replication_mode()) {
      if (storage_->is_empty()) {
        static constexpr std::string_view select_gtid_purged_query{
            "SELECT @@GLOBAL.gtid_purged"};
        storage_->set_purged_gtids(binsrv::gtids::gtid_set{
            connection.execute_select_query_string_result(
                select_gtid_purged_query)});
        logger_->log(
            binsrv::log_severity::info,
            "extracted purged GTIDs from the mysql server for an empty "
            "storage: " +
                boost::lexical_cast<std::string>(storage_->get_purged_gtids()));
      }

      const auto gtids{storage_->get_gtids()};
      const auto encoded_size{gtids.calculate_encoded_size()};

      binsrv::gtids::gtid_set_storage encoded_gtids_buffer(encoded_size);
      util::byte_span destination{encoded_gtids_buffer};
      gtids.encode_to(destination);

      connection.switch_to_gtid_replication(
          server_id, util::const_byte_span{encoded_gtids_buffer},
          verify_checksum, connection_replication_mode_);
    } else {
      if (storage_->is_empty()) {
        connection.switch_to_position_replication(server_id, verify_checksum,
                                                  connection_replication_mode_);
      } else {
        connection.switch_to_position_replication(
            server_id, storage_->get_current_binlog_name().str(),
            storage_->get_current_position(), verify_checksum,
            connection_replication_mode_);
      }
    }
  } catch (const easymysql::core_error &) {
    logger_->log(binsrv::log_severity::error,
                 "unable to switch to replication");
    return false;
  }

  log_replication_info(*logger_, server_id, *storage_, verify_checksum,
                       connection_replication_mode_);
  return true;
}

void collector_context::rewrite_and_process_binlog_event(
    const binsrv::events::event_view &current_event_v,
    binsrv::events::reader_context &context) {
  const auto &replication_config{config_->root().get<"replication">()};

  const auto server_id{replication_config.get<"server_id">()};
  const auto &optional_rewrite_config{replication_config.get<"rewrite">()};
  if (!optional_rewrite_config.has_value()) {
    util::exception_location().raise<std::logic_error>(
        "rewrite configuration is missing");
  }

  const auto &base_file_name{optional_rewrite_config->get<"base_file_name">()};
  const auto file_size{optional_rewrite_config->get<"file_size">().get_value()};

  assert(storage_->is_in_gtid_replication_mode());
  const auto current_common_header_v = current_event_v.get_common_header_view();
  const auto code = current_common_header_v.get_type_code();

  // for ROTATE (both artificial and non-artificial), FORMAT_DESCRIPTION,
  // PREVIOUS_GTIDS_LOG, and STOP events we don't have to do anything -
  // simply return early from this function
  if (code == binsrv::events::code_type::format_description ||
      code == binsrv::events::code_type::previous_gtids_log ||
      code == binsrv::events::code_type::rotate ||
      code == binsrv::events::code_type::stop) {
    // making sure that there will be no events without checksums in the rewrite
    // mode because when recalculating the value of 'transaction_length' field
    // in GTID events we rely on the fact that upcoming events from the same
    // transaction will not change their size after rewriting (currently, to
    // avoid scenarios when one binlog file with checksums enabled is followed
    // by another file without checksums, and we have to combine them in the
    // same storage binlog file, we enforce that all events in the rewrite mode
    // must have checksums)

    // TODO: this restriction can be lifted if we implement whole transaction
    //       rewrite logic (we do not add incomplete transaction events into
    //       the storage buffer unless we receive all of them and perform
    //       necessary checksum addition/removal)
    if (code == binsrv::events::code_type::format_description) {
      const auto format_description_body{binsrv::events::generic_body<
          binsrv::events::code_type::format_description>{
          current_event_v.get_body_raw()}};
      if (!format_description_body.has_checksum_algorithm()) {
        util::exception_location().raise<std::logic_error>(
            "rewrite is supported in gtid replication mode only when "
            "all events received from the MySQL server have checksums");
      }
    }

    const auto readable_flags{current_common_header_v.get_readable_flags()};
    logger_->log(
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
                             storage_->get_current_position() >= file_size)) {
    binsrv::events::event_storage event_buffer;
    std::uint32_t offset{0U};

    // generating next binlog file name based on base file name from the
    // configuration file <rewrite.base_file_name> and current binlog file
    // sequence number from the storage

    // please notice that if storage is empty, then the sequence number will be
    // zero
    binsrv::events::composite_binlog_name binlog_name{};
    if (storage_->is_empty()) {
      // the very first time we receive an event on an empty storage
      binlog_name = binsrv::events::composite_binlog_name{base_file_name, 1U};
    } else if (context.is_fresh()) {
      // this is the very first event we received after reconnection
      // (the storage is not empty and we have an active binlog in it)
      binlog_name = storage_->get_current_binlog_name();
    } else {
      // we are at transaction boundary and reached max binlog file size
      binlog_name = storage_->get_current_binlog_name().next();
    }

    if (!context.is_fresh()) {
      // generate and process ROTATE(non-artificial) event
      offset = static_cast<std::uint32_t>(storage_->get_current_position());
      const auto generated_rotate_event_v{generate_rotate_event(
          event_buffer, context, offset, true /* current timestamp */,
          server_id, false /* non-artificial */, binlog_name)};
      logger_->log(binsrv::log_severity::info,
                   "rewrite: generated rotate event in the rewrite mode");
      process_binlog_event(generated_rotate_event_v, context);
    }

    // generate and process ROTATE(artificial) event
    offset = 0U;
    // artificial ROTATE event must include zero timestamp
    const auto generated_artificial_rotate_event_v{generate_rotate_event(
        event_buffer, context, offset, false /* zero timestamp */, server_id,
        true /* artificial */, binlog_name)};
    logger_->log(
        binsrv::log_severity::info,
        "rewrite: generated artificial rotate event in the rewrite mode");
    process_binlog_event(generated_artificial_rotate_event_v, context);

    // generate and process FORMAT_DESCRIPTION event
    offset = binsrv::events::magic_binlog_offset;
    const auto generated_format_description_event_v{
        generate_format_description_event(event_buffer, context, offset,
                                          server_id)};
    logger_->log(
        binsrv::log_severity::info,
        "rewrite: generated format description event in the rewrite mode");
    process_binlog_event(generated_format_description_event_v, context);

    // generate and process PREVIOUS_GTIDS_LOG event
    offset += static_cast<std::uint32_t>(
        generated_format_description_event_v.get_total_size());
    const auto generated_previous_gtids_log_event_v{
        generate_previous_gtids_log_event(event_buffer, context, offset,
                                          server_id, storage_->get_gtids())};
    logger_->log(
        binsrv::log_severity::info,
        "rewrite: generated previous gtids log event in the rewrite mode");
    process_binlog_event(generated_previous_gtids_log_event_v, context);
  }

  // in rewrite mode we need to update next_event_position (and optional
  // checksum in the footer) in the received event data portion
  binsrv::events::event_storage buffer{};
  const auto event_copy_uv{binsrv::events::rewriter::rewrite(
      storage_->get_last_transaction_sequence_number(), current_event_v, buffer,
      storage_->get_current_position())};
  process_binlog_event(event_copy_uv, context);
}

void collector_context::process_binlog_event(
    const binsrv::events::event_view &current_event_v,
    binsrv::events::reader_context &context) {
  const auto current_common_header_v{current_event_v.get_common_header_view()};
  const auto readable_flags{current_common_header_v.get_readable_flags()};
  logger_->log(
      binsrv::log_severity::info,
      "event  : " +
          std::string{current_common_header_v.get_readable_type_code()} +
          (readable_flags.empty() ? "" : " (" + readable_flags + ")"));
  logger_->log(binsrv::log_severity::debug,
               "event  : [parsed view] " +
                   boost::lexical_cast<std::string>(current_event_v));

  const bool info_only{context.process_event_view(current_event_v)};

  if (info_only) {
    logger_->log(
        binsrv::log_severity::info,
        "event  : [info_only] - will not be written to the binary log file");
  }

  if (context.is_at_transaction_boundary()) {
    logger_->log(
        binsrv::log_severity::info,
        "event  : [end_of_transaction] " +
            boost::lexical_cast<std::string>(context.get_transaction_gtid()));
  }

  // here we additionally check for log level because event materialization
  // is not a trivial operation
  if (binsrv::log_severity::debug >= logger_->get_min_level()) {
    const binsrv::events::event current_event{current_event_v};
    logger_->log(binsrv::log_severity::debug,
                 "event  : [parsed] " +
                     boost::lexical_cast<std::string>(current_event));
  }

  const auto code = current_common_header_v.get_type_code();
  const auto is_artificial{current_common_header_v.get_flags().has_element(
      binsrv::events::common_header_flag_type::artificial)};

  // processing the very first event in the sequence - artificial ROTATE event
  if (code == binsrv::events::code_type::rotate && is_artificial) {
    process_artificial_rotate_event(current_event_v);
  }

  // checking if the event needs to be written to the binlog
  if (!info_only) {
    storage_->write_event(
        current_event_v.get_portion(), context.is_at_transaction_boundary(),
        context.get_transaction_gtid(), current_common_header_v.get_timestamp(),
        context.get_transaction_sequence_number());
  }

  // processing the very last event in the sequence - either a non-artificial
  // ROTATE event or a STOP event. This is the path that closes the local
  // binlog file and (via storage::close_binlog -> flush_event_buffer) is
  // what guarantees the terminator event itself lands on the backend
  if ((code == binsrv::events::code_type::rotate && !is_artificial) ||
      code == binsrv::events::code_type::stop) {
    process_rotate_or_stop_event();
  }
}

void collector_context::process_artificial_rotate_event(
    const binsrv::events::event_view &current_event_v) {
  assert(current_event_v.get_common_header_view().get_type_code() ==
         binsrv::events::code_type::rotate);
  assert(current_event_v.get_common_header_view().get_flags().has_element(
      binsrv::events::common_header_flag_type::artificial));

  const binsrv::events::generic_body<binsrv::events::code_type::rotate>
      current_rotate_body{current_event_v.get_body_raw()};

  bool binlog_opening_needed{true};

  if (storage_->is_binlog_open()) {
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
        storage_->get_current_binlog_name()) {
      // in addition, in position-based replication mode we also need to check
      // the position
      if (storage_->get_replication_mode() ==
          binsrv::replication_mode_type::position) {
        const binsrv::events::generic_post_header<
            binsrv::events::code_type::rotate>
            current_rotate_post_header{current_event_v.get_post_header_raw()};

        if (current_rotate_post_header.get_position_raw() !=
            storage_->get_current_position()) {
          util::exception_location().raise<std::logic_error>(
              "unexpected binlog position in artificial rotate event");
        }
      }

      binlog_opening_needed = false;

      const std::string current_binlog_name{
          storage_->get_current_binlog_name().str()};
      logger_->log(binsrv::log_severity::info,
                   "storage: reused already open binlog file: " +
                       current_binlog_name);

    } else {
      // if names do not match, we need to close the currently open
      // binlog and make sure that binlog_opening_needed is set to true, so
      // that we will open a new one later
      const std::string old_binlog_name{
          storage_->get_current_binlog_name().str()};
      storage_->close_binlog();
      logger_->log(binsrv::log_severity::info,
                   "storage: closed binlog file left open: " + old_binlog_name);
      // binlog_opening_needed remains true in this branch
      assert(binlog_opening_needed);
    }
  }
  if (binlog_opening_needed) {
    const auto binlog_open_result{
        storage_->open_binlog(current_rotate_body.get_parsed_binlog())};

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
    logger_->log(binsrv::log_severity::info, message);
  }
}

void collector_context::process_rotate_or_stop_event() {
  const std::string old_binlog_name{storage_->get_current_binlog_name().str()};
  storage_->close_binlog();
  logger_->log(binsrv::log_severity::info,
               "storage: closed binlog file: " + old_binlog_name);
}

} // namespace operations
