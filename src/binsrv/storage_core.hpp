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

#ifndef BINSRV_STORAGE_CORE_HPP
#define BINSRV_STORAGE_CORE_HPP

#include "binsrv/storage_core_fwd.hpp" // IWYU pragma: export

#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "binsrv/basic_keyring_fwd.hpp"
#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/basic_storage_backend_fwd.hpp"
#include "binsrv/encryption_config_fwd.hpp"
#include "binsrv/encryption_format_type_fwd.hpp"
#include "binsrv/indexed_event_block_fwd.hpp"
#include "binsrv/main_config_fwd.hpp"
#include "binsrv/replication_mode_type_fwd.hpp"

#include "binsrv/events/composite_binlog_name.hpp"

#include "binsrv/gtids/gtid_fwd.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "binsrv/models/binlog_file_encryption_record_fwd.hpp"

#include "binsrv/events/common_types.hpp"

#include "util/byte_range_fwd.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/ctime_timestamp_fwd.hpp"
#include "util/ctime_timestamp_range.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"
#include "util/hex_value.hpp"

namespace binsrv {

struct binlog_encryption_record {
  std::string kek_id;
  util::hex_value_storage file_key_encrypted_with_kek;
  util::optional_hex_value_storage iv_for_file_key_encryption;
  util::optional_hex_value_storage tag_of_file_key_encryption;
  std::string data_cipher;
  util::hex_value_storage iv_for_data_encryption;
  util::optional_hex_value_storage tag_of_data_encryption;

  [[nodiscard]] static models::binlog_file_encryption_record
  to_model(const binlog_encryption_record &record);
  [[nodiscard]] static binlog_encryption_record
  from_model(const models::binlog_file_encryption_record &model);
};

struct binlog_record {
  // binlog file name
  events::composite_binlog_name name;
  // binlog file size in bytes
  std::uint64_t size{0ULL};
  // accumulated GTIDs present in the binlog files before this one
  gtids::optional_gtid_set previous_gtids{};
  // GTIDs present in this binlog file
  gtids::optional_gtid_set added_gtids{};
  // minimum and maximum event timestamps observed in this binlog file
  util::ctime_timestamp_range timestamps{};
  // sequence_number of the last transaction seen in this file -
  // used for GTID rewrite-mode resume state persistence
  events::seq_no_t last_sequence_number{0ULL};
  // optional encryption parameters
  optional_binlog_encryption_record encryption{};
};

class [[nodiscard]] storage_core {
public:
  static constexpr std::string_view default_binlog_index_name{"binlog.index"};
  static constexpr std::string_view default_binlog_index_entry_path{"."};
  static constexpr std::string_view metadata_name{"metadata.json"};
  static constexpr std::string_view binlog_metadata_extension{".json"};

  storage_core(basic_logger_ptr logger, const main_config &config,
               storage_construction_mode_type construction_mode);

  storage_core(const storage_core &) = delete;
  storage_core &operator=(const storage_core &) = delete;
  storage_core(storage_core &&) = delete;
  storage_core &operator=(storage_core &&) = delete;

  ~storage_core();

  [[nodiscard]] storage_construction_mode_type
  get_construction_mode() const noexcept {
    // no mutex protection needed as this method reads data
    // set only once during construction
    return construction_mode_;
  }

  // returning by value for thread-safety
  [[nodiscard]] gtids::gtid_set get_purged_gtids() const {
    const std::shared_lock lock{mutex_};
    return purged_gtids_;
  }
  void set_purged_gtids(const gtids::gtid_set &purged_gtids);

  [[nodiscard]] std::string get_backend_description() const;

  [[nodiscard]] replication_mode_type get_replication_mode() const noexcept {
    // no need to acquire the mutex as replication_mode_ is immutable after
    // construction
    return replication_mode_;
  }
  [[nodiscard]] bool is_in_gtid_replication_mode() const noexcept;

  // returning by value for thread-safety
  [[nodiscard]] binlog_record_container get_binlog_records() const {
    const std::shared_lock lock{mutex_};
    return binlog_records_;
  }
  [[nodiscard]] bool is_empty() const {
    const std::shared_lock lock{mutex_};
    return is_empty_unsafe();
  }
  [[nodiscard]] events::composite_binlog_name get_current_binlog_name() const {
    const std::shared_lock lock{mutex_};
    return get_current_binlog_name_unsafe();
  }
  [[nodiscard]] gtids::gtid_set get_gtids() const {
    const std::shared_lock lock{mutex_};
    return get_gtids_unsafe();
  }
  [[nodiscard]] events::seq_no_t get_last_sequence_number() const {
    const std::shared_lock lock{mutex_};
    return is_empty_unsafe()
               ? 0ULL
               : get_current_binlog_record_unsafe().last_sequence_number;
  }

  [[nodiscard]] std::uint64_t get_flushed_position() const {
    const std::shared_lock lock{mutex_};
    return get_flushed_position_unsafe();
  }

  [[nodiscard]] bool is_binlog_open() const;

  [[nodiscard]] open_binlog_status
  open_binlog(const events::composite_binlog_name &binlog_name);
  void write_event_block(util::const_byte_span event_block_data,
                         const gtids::gtid_set &block_gtids,
                         const util::ctime_timestamp_range &block_timestamps,
                         events::seq_no_t block_max_sequence_number);
  void close_binlog();

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

  // This method will try to read a block of events of length
  // 'range.get_length()' from the specified binlog file 'binlog_name',
  // starting from 'range.get_offset()'.
  // Returns true if the operation was successful, false otherwise.
  // Both 'binlog_name' and 'range' are inout parameters and they will
  // be updated to reflect the actual portion of the binlog that was read
  // if the operation was successful.
  // All parameters will remain untouched if the operation fails.
  // Special cases:
  // - If the offset in the 'range' is less than
  //   'binsrv::events::magic_binlog_offset' (4), the method will return false.
  // - If the specified 'range' is an open range (has no length set), this
  //   method will return false.
  // - If the specified 'binlog_name' is an empty object and offset of the
  //   'range' is not equal to 'binsrv::events::magic_binlog_offset' (4),
  //   the method will return false.
  // - If the specified 'binlog_name' is an empty object and offset of the
  //   'range' is equal to 'binsrv::events::magic_binlog_offset' (4), and
  //   storage has no binlog records, the method will return true,
  //   will set binlog name to an empty object, range to "[4; 0]",
  //   and buffer to an empty buffer.
  // - If the specified 'binlog_name' is an empty object and offset of the
  //   'range' is equal to 'binsrv::events::magic_binlog_offset' (4), and
  //   there is at least one binlog record available, when checking other
  //   rules, we will assume that 'binlog_name' from now on will be equal to
  //   the first available binlog file name.
  // - If the specified (or resolved) 'binlog_name' does not exist in storage,
  //   the method will return false.
  // - If 'range' is an empty range, the method will return true without
  //   attempting to read any data. The range will remain unchanged, the
  //   buffer will be set to an empty object, and 'binlog_name' will be changed
  //   only if it was originally empty and was resolved to the first available
  //   binlog file.
  // - If the specified 'range' has an offset that is beyond the end of the
  //   specified binlog, the method will return false.
  // - If the specified 'range' has valid offset for the given 'binlog_name',
  //   but the length extends beyond the end of the binlog, the method will
  //   return true and will read only the available portion and update
  //   'range' to reflect the actual portion read.
  // - If the 'range.get_offset()' is equal to the length of the binlog file
  //   specified by the 'binlog_name', this method will return true and
  //   will try to read 'range.get_length()' bytes from the
  //   offset 'binsrv::events::magic_binlog_offset' (4) of the next binlog
  //   file, if available. 'range' and 'binlog_name' will be updated
  //   accordingly.
  //   If the next file is not available, the method will return true and will
  //   leave 'binlog_name' as is, change the 'length' component of the 'range'
  //   to 0, and set 'buffer' to an empty buffer, indicating EOF.
  [[nodiscard]] bool
  fetch_event_block(events::composite_binlog_name &binlog_name,
                    util::byte_range &range,
                    util::dynamic_byte_buffer &buffer) const;

  [[nodiscard]] std::string
  get_binlog_uri(const events::composite_binlog_name &binlog_name) const;

  [[nodiscard]] bool is_keyring_initialized() const noexcept {
    // no mutex protection needed as this method reads data
    // set only once during construction
    return static_cast<bool>(keyring_);
  }
  [[nodiscard]] std::string get_keyring_description() const;
  [[nodiscard]] std::string get_active_kek_description() const;
  [[nodiscard]] std::string get_encryption_format_description() const;

  [[nodiscard]] bool has_active_kek() const noexcept {
    // no mutex protection needed as this method reads data
    // set only once during construction
    return !active_kek_id_.empty();
  }

private:
  mutable std::shared_mutex mutex_;

  basic_logger_ptr logger_;
  storage_construction_mode_type construction_mode_;
  basic_keyring_ptr keyring_;
  optional_encryption_format_type encryption_format_;
  std::string active_kek_id_;
  std::string active_data_cipher_{};
  basic_storage_backend_ptr backend_;

  replication_mode_type replication_mode_;
  gtids::gtid_set purged_gtids_{};
  binlog_record_container binlog_records_{};

  [[nodiscard]] bool is_empty_unsafe() const noexcept {
    return binlog_records_.empty();
  }

  using storage_object_name_list = std::vector<std::string>;

  void report_reconciliation_issue(std::string_view message) const;
  // checks whether the object name has the form of an object created by the
  // Binlog Server (storage metadata, binlog index, binlog data and binlog
  // metadata files) - only such objects can be considered garbage
  [[nodiscard]] static bool is_own_object_name(std::string_view object_name);
  // the following methods extract the objects of the corresponding kind from
  // 'object_names' - garbage objects are returned so that they can be
  // removed only after the storage has been validated
  [[nodiscard]] storage_object_name_list
  extract_temporary_objects(storage_object_name_container &object_names) const;
  void
  extract_foreign_objects(storage_object_name_container &object_names) const;
  [[nodiscard]] storage_object_name_list extract_unreferenced_objects(
      storage_object_name_container &object_names) const;
  void remove_garbage_objects(std::span<const std::string> object_names,
                              std::string_view description);

  void initialize_storage_encryption(
      const optional_encryption_config &encryption_config);

  void ensure_streaming_mode() const;
  void ensure_purging_mode() const;

  [[nodiscard]] const binlog_record &
  get_current_binlog_record_unsafe() const noexcept {
    return binlog_records_.back();
  }
  [[nodiscard]] binlog_record &get_current_binlog_record_unsafe() noexcept {
    return binlog_records_.back();
  }
  [[nodiscard]] events::composite_binlog_name
  get_current_binlog_name_unsafe() const {
    return is_empty_unsafe() ? events::composite_binlog_name{}
                             : get_current_binlog_record_unsafe().name;
  }
  [[nodiscard]] gtids::gtid_set get_gtids_unsafe() const {
    if (!is_in_gtid_replication_mode()) {
      return {};
    }

    if (is_empty_unsafe()) {
      return purged_gtids_;
    }
    gtids::gtid_set result{};
    const auto &optional_previous_gtids{
        get_current_binlog_record_unsafe().previous_gtids};
    if (optional_previous_gtids.has_value()) {
      result = *optional_previous_gtids;
    }
    const auto &optional_added_gtids{
        get_current_binlog_record_unsafe().added_gtids};
    if (optional_added_gtids.has_value()) {
      result.add(*optional_added_gtids);
    }
    return result;
  }

  [[nodiscard]] std::uint64_t get_flushed_position_unsafe() const noexcept {
    return is_empty_unsafe() ? 0ULL : get_current_binlog_record_unsafe().size;
  }

  [[nodiscard]] open_binlog_status open_new_binlog_file_internal(
      const events::composite_binlog_name &binlog_name);
  [[nodiscard]] open_binlog_status
  open_existing_binlog_file_internal(std::uint64_t open_stream_offset);

  void load_binlog_index();
  void validate_binlog_index(const storage_object_name_container &object_names);
  void save_binlog_index() const;

  void load_metadata();
  void validate_metadata(
      replication_mode_type replication_mode,
      const optional_encryption_format_type &encryption_format) const;
  void save_metadata() const;

  [[nodiscard]] static std::string generate_binlog_metadata_name(
      const events::composite_binlog_name &binlog_name);
  [[nodiscard]] binlog_record
  load_binlog_metadata(const events::composite_binlog_name &binlog_name) const;
  void validate_binlog_metadata(const binlog_record &record) const;
  void save_binlog_metadata(const binlog_record &record) const;

  // checks whether the storage objects can be the leftovers of an interrupted
  // creation of a single binlog file (its data file and, optionally, its
  // metadata file)
  [[nodiscard]] static bool contains_at_most_one_binlog(
      const storage_object_name_container &object_names);
  // returns false if the binlog file cannot be used (possible only in the
  // querying_only mode, otherwise an exception is thrown)
  [[nodiscard]] bool reconcile_binlog_file_size(const binlog_record &record,
                                                std::uint64_t actual_size,
                                                bool is_last);

  void load_and_validate_binlog_metadata_set(
      const storage_object_name_container &object_names,
      const storage_object_name_container &object_metadata_names);

  [[nodiscard]] optional_binlog_encryption_record
  generate_binlog_encryption_record() const;

  void write_data_to_stream(
      util::const_byte_span data,
      const optional_binlog_encryption_record &encryption_record,
      std::uint64_t offset);
};

} // namespace binsrv

#endif // BINSRV_STORAGE_CORE_HPP
