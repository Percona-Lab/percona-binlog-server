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

#include "binsrv/storage.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "binsrv/basic_logger.hpp"
#include "binsrv/main_config.hpp"
#include "binsrv/replication_mode_type_fwd.hpp"
#include "binsrv/storage_core.hpp"

#include "binsrv/events/common_types.hpp"
#include "binsrv/events/composite_binlog_name.hpp"

#include "binsrv/gtids/gtid.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "util/byte_span.hpp"
#include "util/ctime_timestamp.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

storage::storage(basic_logger_ptr logger, const main_config &config,
                 storage_construction_mode_type construction_mode)
    : core_{std::make_unique<storage_core>(std::move(logger), config,
                                           construction_mode)} {}

storage::~storage() {
  if (core_->get_construction_mode() ==
      storage_construction_mode_type::streaming) {
    // bugprone-empty-catch should not be that strict in destructors
    try {
      flush_event_buffer();
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
  }
}

[[nodiscard]] gtids::gtid_set storage::get_purged_gtids() const {
  return core_->get_purged_gtids();
}

void storage::set_purged_gtids(const gtids::gtid_set &purged_gtids) {
  core_->set_purged_gtids(purged_gtids);
}

[[nodiscard]] std::string storage::get_backend_description() const {
  return core_->get_backend_description();
}

[[nodiscard]] replication_mode_type
storage::get_replication_mode() const noexcept {
  return core_->get_replication_mode();
}

[[nodiscard]] bool storage::is_in_gtid_replication_mode() const noexcept {
  return core_->is_in_gtid_replication_mode();
}

[[nodiscard]] binlog_record_container storage::get_binlog_records() const {
  return core_->get_binlog_records();
}

[[nodiscard]] bool storage::is_empty() const { return core_->is_empty(); }

[[nodiscard]] gtids::gtid_set storage::get_gtids() const {
  return core_->get_gtids();
}

[[nodiscard]] events::composite_binlog_name
storage::get_current_binlog_name() const {
  return core_->get_current_binlog_name();
}

[[nodiscard]] bool storage::is_binlog_open() const {
  return core_->is_binlog_open();
}

[[nodiscard]] open_binlog_status
storage::open_binlog(const events::composite_binlog_name &binlog_name) {
  auto result{core_->open_binlog(binlog_name)};

  if (result != open_binlog_status::created) {
    ready_to_flush_last_sequence_number_ = core_->get_last_sequence_number();
    incomplete_transaction_last_sequence_number_ =
        ready_to_flush_last_sequence_number_;
  } else {
    ready_to_flush_last_sequence_number_ = 0ULL;
    incomplete_transaction_last_sequence_number_ = 0ULL;
  }
  update_last_checkpoint_info();

  assert(std::size(event_buffer_) == 0U);
  event_buffer_.reserve(default_event_buffer_size_in_bytes);
  assert(!has_event_data_to_flush());
  assert(gtids_in_event_buffer_.is_empty());
  assert(ready_to_flush_timestamps_.is_empty());
  assert(incomplete_transaction_timestamps_.is_empty());

  return result;
}

void storage::write_event(util::const_byte_span event_data,
                          bool at_transaction_boundary,
                          const gtids::gtid &transaction_gtid,
                          const util::ctime_timestamp &event_timestamp,
                          events::seq_no_t transaction_sequence_number) {
  ensure_streaming_mode();

  event_buffer_.insert(std::end(event_buffer_), std::cbegin(event_data),
                       std::cend(event_data));
  incomplete_transaction_timestamps_.add_timestamp(event_timestamp);
  // 0 has a special meaning here - it indicates that current event is neither
  // GTID_LOG, nor ANONYMOUS_GTID_LOG, nor GTID_TAGGED_LOG and does not have
  // last sequence number associated with it.
  if (transaction_sequence_number != 0ULL) {
    incomplete_transaction_last_sequence_number_ = transaction_sequence_number;
  }

  if (at_transaction_boundary) {
    last_transaction_boundary_position_in_event_buffer_ =
        std::size(event_buffer_);
    if (is_in_gtid_replication_mode() && !transaction_gtid.is_empty()) {
      gtids_in_event_buffer_ += transaction_gtid;
    }
    ready_to_flush_timestamps_.add_range(incomplete_transaction_timestamps_);
    incomplete_transaction_timestamps_.clear();

    ready_to_flush_last_sequence_number_ =
        incomplete_transaction_last_sequence_number_;
  }

  // now we are writing data from the event buffer to the storage backend if
  // the event buffer has some data in it that can be considered a complete
  // transaction and a checkpoint event (either size-based or time-based)
  // occurred. The file-boundary flush is handled separately, in
  // close_binlog().

  if (has_event_data_to_flush()) {
    const auto ready_to_flush_position{get_ready_to_flush_position()};
    const auto now_ts{std::chrono::steady_clock::now()};

    // here we perform size-based checkpointing calculations based on the
    // calculated "ready_to_flush_position" instead of
    // "get_current_position()" directly to take into account that some event
    // data may remain buffered
    const bool needs_flush{
        (size_checkpointing_enabled() &&
         (ready_to_flush_position >=
          last_checkpoint_position_ + checkpoint_size_bytes_)) ||
        (interval_checkpointing_enabled() &&
         (now_ts >=
          last_checkpoint_timestamp_ + checkpoint_interval_seconds_))};

    if (needs_flush) {
      flush_event_buffer_internal();

      last_checkpoint_position_ = ready_to_flush_position;
      last_checkpoint_timestamp_ = now_ts;
    }
  }
}

void storage::close_binlog() {
  ensure_streaming_mode();

  // This flush is the only path that guarantees the file-final ROTATE/STOP
  // event lands on the backend.
  flush_event_buffer();
  event_buffer_.clear();
  event_buffer_.shrink_to_fit();

  core_->close_binlog();
  update_last_checkpoint_info();
}

void storage::discard_incomplete_transaction_events() {
  ensure_streaming_mode();

  event_buffer_.resize(last_transaction_boundary_position_in_event_buffer_);
  incomplete_transaction_timestamps_.clear();
  incomplete_transaction_last_sequence_number_ =
      ready_to_flush_last_sequence_number_;
}

void storage::flush_event_buffer() {
  ensure_streaming_mode();

  if (has_event_data_to_flush()) {
    flush_event_buffer_internal();
  }
}

[[nodiscard]] std::pair<binlog_record_container, std::string>
storage::purge_binlogs(const events::composite_binlog_name &target) {
  return core_->purge_binlogs(target);
}

[[nodiscard]] std::string storage::get_binlog_uri(
    const events::composite_binlog_name &binlog_name) const {
  return core_->get_binlog_uri(binlog_name);
}

[[nodiscard]] std::string storage::get_keyring_description() const {
  return core_->get_keyring_description();
}

[[nodiscard]] std::string storage::get_active_kek_description() const {
  return core_->get_active_kek_description();
}

[[nodiscard]] std::string storage::get_encryption_format_description() const {
  return core_->get_encryption_format_description();
}

void storage::update_last_checkpoint_info() {
  if (size_checkpointing_enabled()) {
    last_checkpoint_position_ = get_current_position();
  }
  if (interval_checkpointing_enabled()) {
    last_checkpoint_timestamp_ = std::chrono::steady_clock::now();
  }
}

[[nodiscard]] std::uint64_t storage::get_flushed_position() const {
  return core_->get_flushed_position();
}

void storage::flush_event_buffer_internal() {
  assert(!event_buffer_.empty());
  assert(last_transaction_boundary_position_in_event_buffer_ <=
         std::size(event_buffer_));

  const util::const_byte_span transactions_data{
      std::data(event_buffer_),
      last_transaction_boundary_position_in_event_buffer_};
  // writing <last_transaction_boundary_position_in_event_buffer_> bytes from
  // the beginning of the event buffer
  core_->write_event_block(transactions_data, gtids_in_event_buffer_,
                           ready_to_flush_timestamps_,
                           ready_to_flush_last_sequence_number_);

  const auto begin_it{std::cbegin(event_buffer_)};
  const auto portion_it{std::next(
      begin_it, static_cast<std::ptrdiff_t>(
                    last_transaction_boundary_position_in_event_buffer_))};
  // erasing those <last_transaction_boundary_position_in_event_buffer_> bytes
  // from the beginning of this buffer
  event_buffer_.erase(begin_it, portion_it);
  last_transaction_boundary_position_in_event_buffer_ = 0U;
  if (is_in_gtid_replication_mode()) {
    gtids_in_event_buffer_.clear();
  }
  ready_to_flush_timestamps_.clear();
}

void storage::ensure_streaming_mode() const {
  if (core_->get_construction_mode() !=
      storage_construction_mode_type::streaming) {
    util::exception_location().raise<std::logic_error>(
        "operation requires storage to be constructed in streaming mode");
  }
}

} // namespace binsrv
