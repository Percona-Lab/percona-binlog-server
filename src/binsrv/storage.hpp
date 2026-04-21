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
#include <vector>

#include "binsrv/basic_storage_backend_fwd.hpp"
#include "binsrv/composite_binlog_name.hpp"
#include "binsrv/ctime_timestamp_fwd.hpp"
#include "binsrv/ctime_timestamp_range.hpp"
#include "binsrv/replication_mode_type_fwd.hpp"
#include "binsrv/storage_config_fwd.hpp"

#include "binsrv/gtids/gtid_fwd.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv {

class [[nodiscard]] storage {
private:
  struct binlog_record {
    composite_binlog_name name;
    std::uint64_t size{0ULL};
    gtids::optional_gtid_set previous_gtids{};
    gtids::optional_gtid_set added_gtids{};
    ctime_timestamp_range timestamps{};
  };
  using binlog_record_container = std::vector<binlog_record>;

public:
  static constexpr std::string_view default_binlog_index_name{"binlog.index"};
  static constexpr std::string_view default_binlog_index_entry_path{"."};
  static constexpr std::string_view metadata_name{"metadata.json"};
  static constexpr std::string_view binlog_metadata_extension{".json"};

  static constexpr std::size_t default_event_buffer_size_in_bytes{16384U};

  // passing by value as we are going to move from this unique_ptr
  storage(const storage_config &config,
          storage_construction_mode_type construction_mode,
          replication_mode_type replication_mode);

  storage(const storage &) = delete;
  storage &operator=(const storage &) = delete;
  storage(storage &&) = delete;
  storage &operator=(storage &&) = delete;

  // destructor is explicitly declared here and defined as default in .cpp
  // file to complete the rule of 5
  ~storage();

  [[nodiscard]] const gtids::gtid_set &get_purged_gtids() const noexcept {
    return purged_gtids_;
  }
  void set_purged_gtids(const gtids::gtid_set &purged_gtids);

  [[nodiscard]] std::string get_backend_description() const;

  [[nodiscard]] replication_mode_type get_replication_mode() const noexcept {
    return replication_mode_;
  }
  [[nodiscard]] bool is_in_gtid_replication_mode() const noexcept;

  [[nodiscard]] const binlog_record_container &
  get_binlog_records() const noexcept {
    return binlog_records_;
  }
  [[nodiscard]] bool is_empty() const noexcept {
    return binlog_records_.empty();
  }
  [[nodiscard]] const composite_binlog_name &
  get_current_binlog_name() const noexcept {
    return is_empty() ? binlog_name_sentinel_
                      : get_current_binlog_record().name;
  }
  [[nodiscard]] std::uint64_t get_current_position() const noexcept {
    return get_flushed_position() + std::size(event_buffer_);
  }

  [[nodiscard]] gtids::gtid_set get_gtids() const {
    if (!is_in_gtid_replication_mode()) {
      return {};
    }

    if (is_empty()) {
      return get_purged_gtids();
    }
    gtids::gtid_set result{};
    const auto &optional_previous_gtids{
        get_current_binlog_record().previous_gtids};
    if (optional_previous_gtids.has_value()) {
      result = *optional_previous_gtids;
    }
    const auto &optional_added_gtids{get_current_binlog_record().added_gtids};
    if (optional_added_gtids.has_value()) {
      result.add(*optional_added_gtids);
    }
    return result;
  }

  [[nodiscard]] bool is_binlog_open() const noexcept;

  [[nodiscard]] open_binlog_status
  open_binlog(const composite_binlog_name &binlog_name);
  void write_event(util::const_byte_span event_data,
                   bool at_transaction_boundary,
                   const gtids::gtid &transaction_gtid,
                   const ctime_timestamp &event_timestamp);
  void close_binlog();

  void discard_incomplete_transaction_events();
  void flush_event_buffer();

  [[nodiscard]] std::string
  get_binlog_uri(const composite_binlog_name &binlog_name) const;

private:
  storage_construction_mode_type construction_mode_;
  basic_storage_backend_ptr backend_;

  replication_mode_type replication_mode_;
  composite_binlog_name binlog_name_sentinel_{};
  gtids::gtid_set purged_gtids_{};
  binlog_record_container binlog_records_{};

  std::uint64_t checkpoint_size_bytes_{0ULL};
  std::uint64_t last_checkpoint_position_{0ULL};

  std::chrono::steady_clock::duration checkpoint_interval_seconds_{};
  std::chrono::steady_clock::time_point last_checkpoint_timestamp_{};

  using event_buffer_type = std::vector<std::byte>;
  event_buffer_type event_buffer_{};
  std::size_t last_transaction_boundary_position_in_event_buffer_{};
  gtids::gtid_set gtids_in_event_buffer_{};
  ctime_timestamp_range ready_to_flush_timestamps_{};
  ctime_timestamp_range incomplete_transaction_timestamps_{};

  void ensure_streaming_mode() const;

  [[nodiscard]] const binlog_record &
  get_current_binlog_record() const noexcept {
    return binlog_records_.back();
  }
  [[nodiscard]] binlog_record &get_current_binlog_record() noexcept {
    return binlog_records_.back();
  }

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
  [[nodiscard]] std::uint64_t get_flushed_position() const noexcept {
    return is_empty() ? 0ULL : get_current_binlog_record().size;
  }
  [[nodiscard]] std::uint64_t get_ready_to_flush_position() const noexcept {
    return get_flushed_position() +
           last_transaction_boundary_position_in_event_buffer_;
  }
  void flush_event_buffer_internal();

  void load_binlog_index();
  void validate_binlog_index(
      const storage_object_name_container &object_names) const;
  void save_binlog_index() const;

  void load_metadata();
  void validate_metadata(replication_mode_type replication_mode) const;
  void save_metadata() const;

  [[nodiscard]] static std::string
  generate_binlog_metadata_name(const composite_binlog_name &binlog_name);
  [[nodiscard]] binlog_record
  load_binlog_metadata(const composite_binlog_name &binlog_name) const;
  void validate_binlog_metadata(const binlog_record &record) const;
  void save_binlog_metadata(const binlog_record &record) const;

  void load_and_validate_binlog_metadata_set(
      const storage_object_name_container &object_names,
      const storage_object_name_container &object_metadata_names);
};

} // namespace binsrv

#endif // BINSRV_STORAGE_HPP
