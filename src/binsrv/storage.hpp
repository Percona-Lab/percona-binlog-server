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

#ifndef BINSRV_STORAGE_HPP
#define BINSRV_STORAGE_HPP

#include "binsrv/storage_fwd.hpp" // IWYU pragma: export

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "binsrv/basic_keyring_fwd.hpp"
#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/basic_storage_backend_fwd.hpp"
#include "binsrv/encryption_config_fwd.hpp"
#include "binsrv/encryption_format_type_fwd.hpp"
#include "binsrv/main_config_fwd.hpp"
#include "binsrv/replication_mode_type_fwd.hpp"
#include "binsrv/storage_core_fwd.hpp"

#include "binsrv/events/composite_binlog_name.hpp"

#include "binsrv/gtids/gtid_fwd.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "binsrv/models/binlog_file_encryption_record_fwd.hpp"

#include "binsrv/events/common_types.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/ctime_timestamp_fwd.hpp"
#include "util/ctime_timestamp_range.hpp"
#include "util/hex_value.hpp"

namespace binsrv {

class [[nodiscard]] storage {
public:
  static constexpr std::size_t default_event_buffer_size_in_bytes{16384U};

  storage(basic_logger_ptr logger, const main_config &config,
          storage_construction_mode_type construction_mode);

  storage(const storage &) = delete;
  storage &operator=(const storage &) = delete;
  storage(storage &&) = delete;
  storage &operator=(storage &&) = delete;

  ~storage();

  [[nodiscard]] const gtids::gtid_set &get_purged_gtids() const noexcept;
  void set_purged_gtids(const gtids::gtid_set &purged_gtids);

  [[nodiscard]] std::string get_backend_description() const;

  [[nodiscard]] replication_mode_type get_replication_mode() const noexcept;
  [[nodiscard]] bool is_in_gtid_replication_mode() const noexcept;

  [[nodiscard]] const binlog_record_container &
  get_binlog_records() const noexcept;
  [[nodiscard]] bool is_empty() const noexcept;
  [[nodiscard]] events::composite_binlog_name get_current_binlog_name() const;

  [[nodiscard]] std::uint64_t get_current_position() const noexcept {
    return get_flushed_position() + std::size(event_buffer_);
  }

  [[nodiscard]] gtids::gtid_set get_gtids() const;

  [[nodiscard]] events::seq_no_t
  get_last_transaction_sequence_number() const noexcept {
    return incomplete_transaction_last_sequence_number_;
  }

  [[nodiscard]] bool is_binlog_open() const noexcept;

  [[nodiscard]] open_binlog_status
  open_binlog(const events::composite_binlog_name &binlog_name);
  void write_event(util::const_byte_span event_data,
                   bool at_transaction_boundary,
                   const gtids::gtid &transaction_gtid,
                   const util::ctime_timestamp &event_timestamp,
                   events::seq_no_t transaction_sequence_number);
  void close_binlog();

  void discard_incomplete_transaction_events();
  void flush_event_buffer();

  // Removes the contiguous prefix of binlog records [front, target]
  // (inclusive) from the storage and returns a pair:
  //   .first  - the dropped records in chronological order (oldest
  //             first), suitable for direct iteration by the caller
  //             to build a response;
  //   .second - empty on full success; non-empty when the best-effort
  //             step-3 cleanup (removal of victim payload + metadata
  //             objects) failed for at least one object after the
  //             step-2 index rewrite had already committed. The purge
  //             itself is considered successful in this case, but the
  //             storage on disk now contains orphan files that the
  //             constructor's validators will refuse to open on next
  //             startup The string carries the underlying cleanup
  //             error message so the caller.
  [[nodiscard]] std::pair<binlog_record_container, std::string>
  purge_binlogs(const events::composite_binlog_name &target);

  [[nodiscard]] std::string
  get_binlog_uri(const events::composite_binlog_name &binlog_name) const;

  [[nodiscard]] bool is_keyring_initialized() const noexcept;
  [[nodiscard]] std::string get_keyring_description() const;
  [[nodiscard]] std::string get_active_kek_description() const;
  [[nodiscard]] std::string get_encryption_format_description() const;

  [[nodiscard]] bool has_active_kek() const noexcept;

private:
  storage_core_ptr core_;

  std::uint64_t checkpoint_size_bytes_{0ULL};
  std::uint64_t last_checkpoint_position_{0ULL};

  std::chrono::steady_clock::duration checkpoint_interval_seconds_{};
  std::chrono::steady_clock::time_point last_checkpoint_timestamp_{};

  using event_buffer_type = std::vector<std::byte>;
  event_buffer_type event_buffer_{};
  std::size_t last_transaction_boundary_position_in_event_buffer_{};
  gtids::gtid_set gtids_in_event_buffer_{};
  util::ctime_timestamp_range ready_to_flush_timestamps_{};
  util::ctime_timestamp_range incomplete_transaction_timestamps_{};
  events::seq_no_t ready_to_flush_last_sequence_number_{0ULL};
  events::seq_no_t incomplete_transaction_last_sequence_number_{0ULL};

  [[nodiscard]] bool size_checkpointing_enabled() const noexcept {
    return checkpoint_size_bytes_ != 0ULL;
  }

  [[nodiscard]] bool interval_checkpointing_enabled() const noexcept {
    return checkpoint_interval_seconds_ !=
           std::chrono::steady_clock::duration{};
  }
  void update_last_checkpoint_info();

  [[nodiscard]] bool has_event_data_to_flush() const noexcept {
    return last_transaction_boundary_position_in_event_buffer_ != 0ULL;
  }
  [[nodiscard]] std::uint64_t get_flushed_position() const noexcept;
  [[nodiscard]] std::uint64_t get_ready_to_flush_position() const noexcept {
    return get_flushed_position() +
           last_transaction_boundary_position_in_event_buffer_;
  }
  [[nodiscard]] open_binlog_status open_new_binlog_file_internal(
      const events::composite_binlog_name &binlog_name);
  [[nodiscard]] open_binlog_status
  open_existing_binlog_file_internal(std::uint64_t open_stream_offset);

  void flush_event_buffer_internal();

  void ensure_streaming_mode() const;
};

} // namespace binsrv

#endif // BINSRV_STORAGE_HPP
