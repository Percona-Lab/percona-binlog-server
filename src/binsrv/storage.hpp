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
#include "binsrv/replication_mode_type_fwd.hpp"
#include "binsrv/storage_config_fwd.hpp"

#include "binsrv/gtids/gtid_fwd.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv {

class [[nodiscard]] storage {
public:
  static constexpr std::string_view default_binlog_index_name{"binlog.index"};
  static constexpr std::string_view default_binlog_index_entry_path{"."};
  static constexpr std::string_view metadata_name{"metadata.json"};
  static constexpr std::string_view binlog_metadata_extension{".json"};

  static constexpr std::size_t default_event_buffer_size_in_bytes{16384U};

  // passing by value as we are going to move from this unique_ptr
  storage(const storage_config &config, replication_mode_type replication_mode);

  storage(const storage &) = delete;
  storage &operator=(const storage &) = delete;
  storage(storage &&) = delete;
  storage &operator=(storage &&) = delete;

  // desctuctor is explicitly defined as default here to complete the rule of 5
  ~storage();

  [[nodiscard]] std::string get_backend_description() const;

  [[nodiscard]] replication_mode_type get_replication_mode() const noexcept {
    return replication_mode_;
  }
  [[nodiscard]] bool is_in_gtid_replication_mode() const noexcept;

  [[nodiscard]] bool has_current_binlog_name() const noexcept {
    return !binlog_names_.empty();
  }
  [[nodiscard]] std::string_view get_current_binlog_name() const noexcept {
    return has_current_binlog_name() ? binlog_names_.back()
                                     : std::string_view{};
  }
  [[nodiscard]] std::uint64_t get_current_position() const noexcept {
    return position_;
  }

  [[nodiscard]] const gtids::gtid_set &get_gtids() const noexcept {
    return gtids_;
  }

  [[nodiscard]] static bool
  check_binlog_name(std::string_view binlog_name) noexcept;

  [[nodiscard]] bool is_binlog_open() const noexcept;

  [[nodiscard]] open_binlog_status open_binlog(std::string_view binlog_name);
  void write_event(util::const_byte_span event_data,
                   bool at_transaction_boundary,
                   const gtids::gtid &transaction_gtid);
  void close_binlog();

  void discard_incomplete_transaction_events();
  void flush_event_buffer();

private:
  basic_storage_backend_ptr backend_;

  replication_mode_type replication_mode_;
  using binlog_name_container = std::vector<std::string>;
  binlog_name_container binlog_names_;
  std::uint64_t position_{0ULL};

  gtids::gtid_set gtids_{};

  std::uint64_t checkpoint_size_bytes_{0ULL};
  std::uint64_t last_checkpoint_position_{0ULL};

  std::chrono::steady_clock::duration checkpoint_interval_seconds_{};
  std::chrono::steady_clock::time_point last_checkpoint_timestamp_{};

  using event_buffer_type = std::vector<std::byte>;
  event_buffer_type event_buffer_{};
  std::size_t last_transaction_boundary_position_in_event_buffer_{};
  gtids::gtid_set gtids_in_event_buffer_{};

  [[nodiscard]] bool size_checkpointing_enabled() const noexcept {
    return checkpoint_size_bytes_ != 0ULL;
  }

  [[nodiscard]] bool interval_checkpointing_enabled() const noexcept {
    return checkpoint_interval_seconds_ !=
           std::chrono::steady_clock::duration{};
  }

  [[nodiscard]] bool has_event_data_to_flush() const noexcept {
    return last_transaction_boundary_position_in_event_buffer_ != 0ULL;
  }
  [[nodiscard]] std::uint64_t get_flushed_position() const noexcept {
    return get_current_position() - std::size(event_buffer_);
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
  generate_binlog_metadata_name(std::string_view binlog_name);
  void load_binlog_metadata(std::string_view binlog_name);
  void save_binlog_metadata(std::string_view binlog_name) const;

  void load_and_validate_binlog_metadata_set(
      const storage_object_name_container &object_names,
      const storage_object_name_container &object_metadata_names);
};

} // namespace binsrv

#endif // BINSRV_STORAGE_HPP
