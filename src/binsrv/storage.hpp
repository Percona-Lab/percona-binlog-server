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

#include "binsrv/gtids/gtid_set.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv {

class [[nodiscard]] storage {
public:
  static constexpr std::string_view default_binlog_index_name{"binlog.index"};
  static constexpr std::string_view default_binlog_index_entry_path{"."};
  static constexpr std::string_view metadata_name{"metadata.json"};

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
  void open_binlog(std::string_view binlog_name);
  void write_event(util::const_byte_span event_data);
  void close_binlog();

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

  [[nodiscard]] bool size_checkpointing_enabled() const noexcept {
    return checkpoint_size_bytes_ != 0;
  }

  [[nodiscard]] bool interval_checkpointing_enabled() const noexcept {
    return checkpoint_interval_seconds_ !=
           std::chrono::steady_clock::duration{};
  }

  void load_binlog_index();
  void validate_binlog_index(const storage_object_name_container &object_names);
  void save_binlog_index();
  void load_metadata();
  void validate_metadata(replication_mode_type replication_mode);
  void save_metadata();
};

} // namespace binsrv

#endif // BINSRV_STORAGE_HPP
