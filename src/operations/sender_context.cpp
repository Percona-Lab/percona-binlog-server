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

#include "operations/sender_context.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "binsrv/basic_logger.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/replication_mode_type.hpp"
#include "binsrv/storage.hpp"

#include "binsrv/events/protocol_traits_fwd.hpp"

#include "binsrv/gtids/gtid_set.hpp"

#include "operations/sender_single_file_context.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace operations {

sender_context::sender_context(
    binsrv::basic_logger_ptr logger, binsrv::storage_ptr storage,
    std::size_t block_size, binsrv::replication_mode_type replication_mode,
    std::string_view binlog_name, std::uint64_t position,
    binsrv::gtids::gtid_set gtids, bool session_source_binlog_checksum)
    : logger_{std::move(logger)}, storage_{std::move(storage)},
      construction_block_size_{block_size},
      construction_replication_mode_{replication_mode},
      construction_gtids_{std::move(gtids)},
      current_binlog_name_{
          binlog_name.empty()
              ? binsrv::events::composite_binlog_name{}
              : binsrv::events::composite_binlog_name::parse(binlog_name)},
      current_position_{position},
      current_binlog_checksum_{session_source_binlog_checksum} {
  assert(logger_);
  assert(storage_);
  logger_->log_format(
      binsrv::log_severity::info,
      "sender : starting serving events in {}-based mode from {}:{}",
      to_string_view(replication_mode), binlog_name, current_position_);
  if (construction_replication_mode_ == binsrv::replication_mode_type::gtid) {
    logger_->log_format(binsrv::log_severity::info,
                        "sender : initial GTID set - {}",
                        construction_gtids_.str());
  }
}

sender_context::~sender_context() = default;

[[nodiscard]] sender_context_check_result_type
sender_context::check_for_non_empty_and_validate() {
  auto result{sender_context_check_result_type::ready};
  if (!current_binlog_name_.is_empty()) {
    // similarly to MySQL Server, when the replica specifies the binlog file
    // name to start from (in both COM_BINLOG_DUMP and COM_BINLOG_DUMP_GTID),
    // we only check that this binlog file exists in the storage - no
    // GTID-related checks are performed in this case
    if (!storage_->contains_binlog(current_binlog_name_)) {
      logger_->log_format(
          binsrv::log_severity::error,
          "sender : the binlog '{}' requested by the replica does not exist",
          current_binlog_name_.str());
      result = sender_context_check_result_type::error_non_existing_binlog;
    }
  } else {
    result = (construction_replication_mode_ ==
                      binsrv::replication_mode_type::position
                  ? check_for_non_empty_and_validate_position()
                  : check_for_non_empty_and_validate_gtid());
  }
  check_passed_ = (result == sender_context_check_result_type::ready);
  return result;
}

[[nodiscard]] event_fetching_result_type
sender_context::get_event(util::const_byte_span &event) {
  if (!check_passed_) {
    util::exception_location().raise<std::logic_error>(
        "check_for_non_empty_and_validate() has not been passed yet");
  }
  assert(!current_binlog_name_.is_empty());

  while (true) {
    if (!single_file_context_) {
      single_file_context_ = std::make_unique<sender_single_file_context>(
          logger_, storage_, construction_block_size_,
          construction_replication_mode_, current_binlog_name_,
          current_position_, construction_gtids_, current_binlog_checksum_);
      logger_->log_format(binsrv::log_severity::info,
                          "sender : switched to a new binlog file {}",
                          current_binlog_name_.str());
    }

    util::const_byte_span local_event{};
    const auto result{single_file_context_->get_event(local_event)};
    switch (result) {
    case event_fetching_result_type::success:
      event = local_event;
      return event_fetching_result_type::success;

    case event_fetching_result_type::waiting:
      event = {};
      return event_fetching_result_type::waiting;

    case event_fetching_result_type::eof:
      current_binlog_checksum_ =
          single_file_context_->is_checksum_enabled_in_format_description();
      current_position_ = binsrv::events::magic_binlog_offset;
      current_binlog_name_ =
          storage_->get_next_binlog_name(current_binlog_name_);
      if (current_binlog_name_.is_empty()) {
        util::exception_location().raise<std::logic_error>(
            "storage returned empty binlog name after non-active EOF");
      }

      single_file_context_.reset();
      break;
    }
  }
}

[[nodiscard]] bool
sender_context::generate_heartbeat(util::const_byte_span &event) {
  if (!single_file_context_) {
    // heartbeats describe the position reached inside a binlog file; before the
    // first file has been resolved (empty storage) there is nothing to report
    // yet - this is a transient "not ready", not an error, so stay quiet and
    // let the caller keep waiting
    event = {};
    return false;
  }
  // the active single-file context owns the read-up-to position, server_id and
  // checksum flag that the heartbeat has to carry, so it assembles the event
  return single_file_context_->generate_heartbeat(event);
}

[[nodiscard]] sender_context_check_result_type
sender_context::check_for_non_empty_and_validate_position() {
  assert(construction_replication_mode_ ==
         binsrv::replication_mode_type::position);
  assert(current_binlog_name_.is_empty());

  if (storage_->is_empty()) {
    logger_->log(binsrv::log_severity::info,
                 "sender : waiting for first binlog in empty storage");
    return sender_context_check_result_type::still_empty;
  }

  current_binlog_name_ = storage_->get_next_binlog_name(current_binlog_name_);
  assert(!current_binlog_name_.is_empty());
  logger_->log_format(binsrv::log_severity::info,
                      "sender : empty binlog name resolved to {}",
                      current_binlog_name_.str());
  return sender_context_check_result_type::ready;
}

[[nodiscard]] sender_context_check_result_type
sender_context::check_for_non_empty_and_validate_gtid() {
  assert(construction_replication_mode_ == binsrv::replication_mode_type::gtid);
  assert(current_binlog_name_.is_empty());

  if (storage_->is_empty()) {
    logger_->log(binsrv::log_severity::info,
                 "sender : waiting for first binlog in empty storage");
    return sender_context_check_result_type::still_empty;
  }

  // in GTID-based replication mode, we first check if source has not purged
  // GTIDs that the replica has not yet received
  extracted_purged_gtids_ = storage_->get_purged_gtids();
  const auto missing_purged_gtids{extracted_purged_gtids_ -
                                  construction_gtids_};
  if (!missing_purged_gtids.is_empty()) {
    logger_->log_format(
        binsrv::log_severity::error,
        "sender : the storage has purged GTIDs '{}' required by the replica",
        missing_purged_gtids.str());
    return sender_context_check_result_type::error_source_purged_required_gtids;
  }

  // unlike MySQL Server, we do not check if the replica has more GTIDs than
  // the source (ER_REPLICA_HAS_MORE_GTIDS_THAN_SOURCE): PBS never originates
  // transactions and its 'replication_source.server_uuid' is used only to
  // identify it in the replication topology - it never appears in any GTID;
  // a replica that is ahead with GTIDs of other UUIDs (e.g. because PBS lags
  // behind the upstream server) is accepted and waits for them, similarly to
  // a MySQL Server intermediate replica

  // in GTID-based replication mode we need to perform binlog
  // auto-positioning based on the provided GTID set
  current_binlog_name_ =
      storage_->get_auto_positioning_binlog_name(construction_gtids_);
  assert(!current_binlog_name_.is_empty());
  logger_->log_format(
      binsrv::log_severity::info,
      "sender : empty binlog name resolved to {} (GTID auto-positioning)",
      current_binlog_name_.str());
  return sender_context_check_result_type::ready;
}

} // namespace operations
