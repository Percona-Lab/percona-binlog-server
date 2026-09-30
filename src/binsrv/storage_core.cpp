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

#include "binsrv/storage_core.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iterator>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "binsrv/basic_keyring.hpp"
#include "binsrv/basic_logger.hpp"
#include "binsrv/basic_storage_backend.hpp"
#include "binsrv/binlog_file_metadata.hpp"
#include "binsrv/encryption_format_type.hpp"
#include "binsrv/keyring_factory.hpp"
#include "binsrv/keyring_record.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/main_config.hpp"
#include "binsrv/replication_config.hpp"
#include "binsrv/replication_mode_type.hpp"
#include "binsrv/storage_backend_factory.hpp"
#include "binsrv/storage_config.hpp"
#include "binsrv/storage_metadata.hpp"

#include "binsrv/events/common_types.hpp"
#include "binsrv/events/composite_binlog_name.hpp"
#include "binsrv/events/protocol_traits_fwd.hpp"

#include "binsrv/gtids/gtid_set.hpp"

#include "binsrv/models/binlog_file_encryption_record.hpp"

#include "opensslpp/cipher_context.hpp"
#include "opensslpp/crypto_rng.hpp"

#include "util/byte_range.hpp"
#include "util/byte_span.hpp"
#include "util/ctime_timestamp_range.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

namespace {

// returns the parsed binlog name if 'object_name' is a well-formed binlog
// file name (<base_name>.<6-digit sequence number>) in its canonical form
[[nodiscard]] std::optional<events::composite_binlog_name>
try_parse_binlog_name(std::string_view object_name) {
  try {
    auto binlog_name{events::composite_binlog_name::parse(object_name)};
    if (binlog_name.str() != object_name) {
      return std::nullopt;
    }
    return binlog_name;
  } catch (const std::invalid_argument &) {
    return std::nullopt;
  }
}

} // anonymous namespace

[[nodiscard]] models::binlog_file_encryption_record
binlog_encryption_record::to_model(const binlog_encryption_record &record) {
  models::binlog_file_encryption_record model{};

  auto &file_key_envelope{model.get<"file_key_envelope">()};
  file_key_envelope.get<"kek_id">() = record.kek_id;
  file_key_envelope.get<"data_hex">() = record.file_key_encrypted_with_kek;
  if (record.iv_for_file_key_encryption.has_value()) {
    file_key_envelope.get<"iv_hex">() = *record.iv_for_file_key_encryption;
  }
  if (record.tag_of_file_key_encryption.has_value()) {
    file_key_envelope.get<"tag_hex">() = *record.tag_of_file_key_encryption;
  }

  auto &file_data_envelope{model.get<"file_data_envelope">()};
  file_data_envelope.get<"cipher">() = record.data_cipher;
  file_data_envelope.get<"iv_hex">() = record.iv_for_data_encryption;
  if (record.tag_of_data_encryption.has_value()) {
    file_data_envelope.get<"tag_hex">() = *record.tag_of_data_encryption;
  }
  return model;
}

[[nodiscard]] binlog_encryption_record binlog_encryption_record::from_model(
    const models::binlog_file_encryption_record &model) {
  binlog_encryption_record record{};

  const auto &file_key_envelope{model.get<"file_key_envelope">()};
  record.kek_id = file_key_envelope.get<"kek_id">();
  const auto file_key_raw{file_key_envelope.get<"data_hex">().get_data()};
  record.file_key_encrypted_with_kek.assign(std::cbegin(file_key_raw),
                                            std::cend(file_key_raw));
  if (file_key_envelope.get<"iv_hex">().has_value()) {
    const auto file_key_iv_raw{file_key_envelope.get<"iv_hex">()->get_data()};
    record.iv_for_file_key_encryption.emplace(std::cbegin(file_key_iv_raw),
                                              std::cend(file_key_iv_raw));
  }
  if (file_key_envelope.get<"tag_hex">().has_value()) {
    const auto file_key_tag_raw{file_key_envelope.get<"tag_hex">()->get_data()};
    record.tag_of_file_key_encryption.emplace(std::cbegin(file_key_tag_raw),
                                              std::cend(file_key_tag_raw));
  }

  const auto &file_data_envelope{model.get<"file_data_envelope">()};
  record.data_cipher = file_data_envelope.get<"cipher">();
  const auto file_data_iv_raw{file_data_envelope.get<"iv_hex">().get_data()};
  record.iv_for_data_encryption.assign(std::cbegin(file_data_iv_raw),
                                       std::cend(file_data_iv_raw));
  if (file_data_envelope.get<"tag_hex">().has_value()) {
    const auto file_data_tag_raw{
        file_data_envelope.get<"tag_hex">()->get_data()};
    record.tag_of_data_encryption.emplace(std::cbegin(file_data_tag_raw),
                                          std::cend(file_data_tag_raw));
  }
  return record;
}

storage_core::storage_core(basic_logger_ptr logger, const main_config &config,
                           storage_construction_mode_type construction_mode)
    : logger_{std::move(logger)}, construction_mode_{construction_mode},
      backend_{} {
  assert(logger_);

  const auto &replication_config{config.root().get<"replication">()};
  // we need a copy of replication mode as replication_mode_ will be
  // overwritten by load_metadata() later
  const auto replication_mode{replication_config.get<"mode">()};
  replication_mode_ = replication_mode;

  const auto &storage_config{config.root().get<"storage">()};

  const auto &keyring_config{config.root().get<"keyring">()};
  if (keyring_config.has_value()) {
    keyring_ = keyring_factory::create(keyring_config->get<"uri">());
  }
  const auto &encryption_config{storage_config.get<"encryption">()};
  initialize_storage_encryption(encryption_config);

  backend_ = storage_backend_factory::create(storage_config);

  auto storage_objects{backend_->list_objects()};
  remove_temporary_objects(storage_objects);

  if (storage_objects.empty()) {
    // initialized on a new / empty storage - just save metadata and return
    if (construction_mode_ == storage_construction_mode_type::streaming) {
      save_metadata();
    }
    return;
  }

  const auto metadata_it{std::as_const(storage_objects).find(metadata_name)};
  if (metadata_it == std::cend(storage_objects)) {
    util::exception_location().raise<std::logic_error>(
        "storage is not empty but does not contain metadata");
  }
  storage_objects.erase(metadata_it);

  // as load_metadata() will be updating 'encryption_format_', saving it here
  // to use for validation later
  const auto encryption_format{encryption_format_};
  load_metadata();
  validate_metadata(replication_mode, encryption_format);
  // in case when storage metadata file is present and did not have encryption
  // format specified, but it is set in the configuration file, we need to
  // update storage metadata
  if (!encryption_format_.has_value() && encryption_format.has_value()) {
    encryption_format_ = encryption_format;
    save_metadata();
  }

  // if after metadata erasure 'storage_objects' is empty, then this means
  // that it has only metadata in it that passes validation and we can
  // consider it as an initialized empty storage, so just return
  if (storage_objects.empty()) {
    return;
  }

  // binlog index is a derived object - its content is never read back as
  // binlog metadata objects are the source of truth
  bool binlog_index_present{false};
  const auto binlog_index_it{storage_objects.find(default_binlog_index_name)};
  if (binlog_index_it != std::cend(storage_objects)) {
    storage_objects.erase(binlog_index_it);
    binlog_index_present = true;
  }

  load_and_reconcile_binlog_set(storage_objects);
  assert(binlog_records_.empty() ||
         !binlog_records_.front().added_gtids.has_value() ||
         purged_gtids_ == binlog_records_.front().added_gtids);

  // in streaming mode we bring the derived binlog index in line with the
  // reconciled set of binlog files (it may be stale or missing after an
  // improper shutdown)
  if (construction_mode_ == storage_construction_mode_type::streaming &&
      (!binlog_records_.empty() || binlog_index_present)) {
    save_binlog_index();
  }
}

storage_core::~storage_core() = default;

void storage_core::set_purged_gtids(const gtids::gtid_set &purged_gtids) {
  if (!is_in_gtid_replication_mode()) {
    util::exception_location().raise<std::logic_error>(
        "cannot set purged GTIDs in position-based replication mode");
  }

  const std::unique_lock lock{mutex_};
  if (!is_empty_unsafe()) {
    util::exception_location().raise<std::logic_error>(
        "cannot set purged GTIDs in a non-empty storage");
  }
  purged_gtids_ = purged_gtids;
}

[[nodiscard]] std::string storage_core::get_backend_description() const {
  // no mutex protection needed as this method calls a const
  // method on an instance of basic_storage_backend that reads only data
  // that was set only once during construction
  return backend_->get_description();
}

[[nodiscard]] bool storage_core::is_in_gtid_replication_mode() const noexcept {
  // no need to acquire the mutex as replication_mode_ is immutable after
  // construction
  return replication_mode_ == replication_mode_type::gtid;
}

[[nodiscard]] bool storage_core::is_binlog_open() const {
  const std::shared_lock lock{mutex_};
  return backend_->is_stream_open();
}

[[nodiscard]] open_binlog_status
storage_core::open_binlog(const events::composite_binlog_name &binlog_name) {
  ensure_streaming_mode();

  const std::unique_lock lock{mutex_};

  auto result{open_binlog_status::opened_with_data_present};

  // here we either create a new binlog file if its name is not presentin the
  // "binlog_records_", or we open an existing one and append to it, in which
  // case we need to make sure that the current position is properly set
  const bool binlog_exists{
      std::ranges::find(std::as_const(binlog_records_), binlog_name,
                        &binlog_record::name) != std::cend(binlog_records_)};

  // in the case when binlog exists, the name must be equal to the last item in
  // "binlog_records_" list and "position_" must be set to a non-zero value
  if (binlog_exists) {
    if (binlog_name != get_current_binlog_name_unsafe()) {
      util::exception_location().raise<std::logic_error>(
          "cannot open an existing binlog that is not the latest one for "
          "append");
    }
    if (get_flushed_position_unsafe() == 0ULL) {
      util::exception_location().raise<std::logic_error>(
          "invalid position set when opening an existing binlog");
    }
  }

  const auto mode{binlog_exists ? storage_backend_open_stream_mode::append
                                : storage_backend_open_stream_mode::create};
  const auto open_stream_offset{backend_->open_stream(binlog_name.str(), mode)};

  if (binlog_exists) {
    result = open_existing_binlog_file_internal(open_stream_offset);
  } else {
    result = open_new_binlog_file_internal(binlog_name);
  }

  return result;
}

void storage_core::write_event_block(
    util::const_byte_span event_block_data, const gtids::gtid_set &block_gtids,
    const util::ctime_timestamp_range &block_timestamps,
    events::seq_no_t block_max_sequence_number) {
  ensure_streaming_mode();

  const std::unique_lock lock{mutex_};

  write_data_to_stream(event_block_data,
                       get_current_binlog_record_unsafe().encryption,
                       get_current_binlog_record_unsafe().size);
  get_current_binlog_record_unsafe().size += std::size(event_block_data);
  if (is_in_gtid_replication_mode()) {
    auto &optional_added_gtids{get_current_binlog_record_unsafe().added_gtids};
    if (optional_added_gtids.has_value()) {
      *optional_added_gtids += block_gtids;
    }
  }
  get_current_binlog_record_unsafe().timestamps.add_range(block_timestamps);
  get_current_binlog_record_unsafe().last_sequence_number =
      block_max_sequence_number;

  save_binlog_metadata(get_current_binlog_record_unsafe());
}

void storage_core::close_binlog() {
  ensure_streaming_mode();

  const std::unique_lock lock{mutex_};

  backend_->close_stream();
}

[[nodiscard]] std::pair<binlog_record_container, std::string>
storage_core::purge_binlogs(const events::composite_binlog_name &target) {
  ensure_purging_mode();

  const std::unique_lock lock{mutex_};
  if (is_empty_unsafe()) {
    util::exception_location().raise<std::runtime_error>(
        "cannot purge: binlog storage is empty");
  }
  const auto &front_base_name{binlog_records_.front().name.get_base_name()};
  if (target.get_base_name() != front_base_name) {
    util::exception_location().raise<std::runtime_error>(
        "cannot purge: target binlog name has a different base name than "
        "the binlog records in the storage");
  }
  const auto target_it{std::ranges::find(std::as_const(binlog_records_), target,
                                         &binlog_record::name)};
  if (target_it == std::cend(binlog_records_)) {
    util::exception_location().raise<std::runtime_error>(
        "cannot purge: target binlog name is not present in the storage");
  }
  // refuse to purge the current tail: emptying the storage would lose
  // the resume position (current binlog name / position in position
  // mode, executed GTID set in GTID mode) and force the next 'fetch' /
  // 'pull' to re-stream from the very beginning of the source's
  // retained binlog history.
  if (target_it == std::prev(std::cend(binlog_records_))) {
    util::exception_location().raise<std::runtime_error>(
        "cannot purge: target is the current tail binlog file; at least "
        "one binlog file must remain in the storage to preserve the "
        "resume position");
  }

  // step 1: extract the prefix [begin, target_it + 1) - this
  // becomes the set of records we are going to drop on disk; the
  // returned vector preserves the original order so the caller can
  // use it directly to produce a response
  const auto victim_count{static_cast<std::size_t>(
      std::distance(std::cbegin(binlog_records_), target_it) + 1)};
  binlog_record_container removed_records;
  removed_records.reserve(victim_count);
  std::move(std::begin(binlog_records_),
            std::begin(binlog_records_) +
                static_cast<std::ptrdiff_t>(victim_count),
            std::back_inserter(removed_records));
  binlog_records_.erase(std::begin(binlog_records_),
                        std::begin(binlog_records_) +
                            static_cast<std::ptrdiff_t>(victim_count));

  // step 2: commit the purge by moving the purge horizon past the
  // target binlog file. 'save_metadata' goes through the backend's
  // atomic-overwrite 'put_object', so from this point on the purge is
  // considered committed - the victim objects are no longer a part of
  // the storage even if the process is killed before they are removed
  // (in which case they are removed during the next startup).
  purge_horizon_ = removed_records.back().ordinal + 1ULL;
  save_metadata();

  // step 3: best-effort removal of the victim data objects and then of
  // their metadata objects; any failure here is intentionally
  // swallowed - the purge has already been committed and reporting a
  // "file could not be removed" error to the caller would falsely
  // suggest that the purge itself failed.
  // Data objects are removed first so that a leftover data object can
  // never outlive its metadata object - the startup reconciliation
  // treats a non-empty data object without metadata as corruption.
  // Each batch is handed to 'basic_storage_backend::remove_objects',
  // which runs the backend's durability barrier exactly once at the
  // end of the batch - so the whole purge amortises to two fsync(2)
  // calls on the local filesystem backend (and no-ops on S3) instead
  // of O(N) syncs.
  std::vector<std::string> victim_data_object_names;
  std::vector<std::string> victim_metadata_object_names;
  victim_data_object_names.reserve(std::size(removed_records));
  victim_metadata_object_names.reserve(std::size(removed_records));
  for (const auto &victim : removed_records) {
    victim_data_object_names.emplace_back(victim.name.str());
    victim_metadata_object_names.emplace_back(
        generate_binlog_metadata_name(victim.name));
  }
  std::string cleanup_warning_message;
  try {
    backend_->remove_objects(victim_data_object_names);
    // this line is not reached if at least one data object could not be
    // removed, so that its metadata object stays in place
    backend_->remove_objects(victim_metadata_object_names);
  } catch (const std::exception &e) {
    // 'remove_objects' re-raises the first per-name failure (if any)
    // after running the durability barrier; we do not propagate it
    // to the caller because the purge has already been committed
    // and any leftover objects will be removed during the next
    // startup. We just capture the underlying message so the caller
    // can surface it under a 'warning' status in the JSON response.
    cleanup_warning_message = e.what();
  }

  // step 4: bring the derived binlog index in line with the surviving
  // binlog records
  try_save_binlog_index();

  return {std::move(removed_records), std::move(cleanup_warning_message)};
}

[[nodiscard]] bool
storage_core::fetch_event_block(events::composite_binlog_name &binlog_name,
                                util::byte_range &range,
                                util::dynamic_byte_buffer &buffer) const {
  static const util::byte_range magic_empty_range{events::magic_binlog_offset,
                                                  0ULL};
  // If the offset in the 'range' is less than
  // 'binsrv::events::magic_binlog_offset' (4), the method will return false.
  if (range.get_offset() < events::magic_binlog_offset) {
    return false;
  }

  // If the specified 'range' is an open range (has no length set), this
  // method will return false.
  if (!range.has_length()) {
    return false;
  }

  // If the specified 'binlog_name' is an empty object and offset of the
  // 'range' is not equal to 'binsrv::events::magic_binlog_offset' (4),
  // the method will return false.
  if (binlog_name.is_empty() &&
      range.get_offset() != events::magic_binlog_offset) {
    return false;
  }

  const std::shared_lock lock{mutex_};

  // If the specified 'binlog_name' is an empty object and offset of the
  // 'range' is equal to 'binsrv::events::magic_binlog_offset' (4), and
  // storage has no binlog records, the method will return true,
  // will set binlog name to an empty object, range to "[4; 0]",
  // and buffer to an empty buffer.
  if (binlog_records_.empty()) {
    // EOF is returned only when 'binlog_name' is an empty object
    if (!binlog_name.is_empty()) {
      return false;
    }
    range = magic_empty_range;
    buffer.clear();
    return true;
  }

  binlog_record_container::const_iterator record_it{};
  // If the specified 'binlog_name' is an empty object and offset of the
  // 'range' is equal to 'binsrv::events::magic_binlog_offset' (4), and
  // there is at least one binlog record available, when checking other
  // rules, we will assume that 'binlog_name' from now on will be equal to
  // the first available binlog file name.
  if (binlog_name.is_empty()) {
    record_it = std::cbegin(binlog_records_);
  } else {
    // If the specified (or resolved) 'binlog_name' does not exist in storage,
    // the method will return false.
    record_it = std::ranges::find(std::as_const(binlog_records_), binlog_name,
                                  &binlog_record::name);
    if (record_it == std::cend(binlog_records_)) {
      return false;
    }
  }
  auto resolved_binlog_name{record_it->name};

  // If 'range' is an empty range, the method will return true without
  // attempting to read any data. The range will remain unchanged, the
  // buffer will be set to an empty object, and 'binlog_name' will be changed
  // only if it was originally empty and was resolved to the first available
  // binlog file.
  if (range.is_empty()) {
    binlog_name = std::move(resolved_binlog_name);
    buffer.clear();
    return true;
  }

  // If the specified 'range' has an offset that is beyond the end of the
  // specified binlog, the method will return false.
  auto read_offset{range.get_offset()};
  if (read_offset > record_it->size) {
    return false;
  }

  // If the 'range.get_offset()' is equal to the length of the binlog file
  // specified by the 'binlog_name', this method will return true and
  // will try to read 'range.get_length()' bytes from the
  // offset 'binsrv::events::magic_binlog_offset' (4) of the next binlog
  // file, if available. 'range' and 'binlog_name' will be updated
  // accordingly.
  if (read_offset == record_it->size) {
    ++record_it;
    // If the next file is not available, the method will return true and will
    // leave 'binlog_name' as is, change the 'length' component of the 'range'
    // to 0, and set 'buffer' to an empty buffer, indicating EOF.
    if (record_it == std::cend(binlog_records_)) {
      range = util::byte_range{read_offset, 0ULL};
      buffer.clear();
      return true;
    }
    resolved_binlog_name = record_it->name;
    read_offset = events::magic_binlog_offset;
  }

  const std::uint64_t read_length{
      std::min(range.get_length(), record_it->size - read_offset)};
  const util::byte_range resolved_range{read_offset, read_length};

  auto result_buffer{
      backend_->get_object(resolved_binlog_name.str(), resolved_range)};

  binlog_name = std::move(resolved_binlog_name);
  range = resolved_range;
  buffer = std::move(result_buffer);
  return true;
}

[[nodiscard]] std::string storage_core::get_binlog_uri(
    const events::composite_binlog_name &binlog_name) const {
  // no mutex protection needed as this method calls a const
  // method on an instance of basic_storage_backend that reads only data
  // that was set only once during construction
  return backend_->get_object_uri(binlog_name.str());
}

[[nodiscard]] std::string storage_core::get_keyring_description() const {
  // no mutex protection needed as this method calls a const
  // method on an immutable keyring instance
  return is_keyring_initialized() ? keyring_->get_description()
                                  : "keyring is not initialized";
}

[[nodiscard]] std::string storage_core::get_active_kek_description() const {
  // no mutex protection needed as this method calls a chain of const
  // methods on an immutable keyring instance
  return has_active_kek() ? keyring_->get_key(active_kek_id_).get_description()
                          : "active KEK is not set";
}

[[nodiscard]] std::string
storage_core::get_encryption_format_description() const {
  // no mutex protection needed as this method reads data
  // set only once during construction
  return encryption_format_.has_value()
             ? std::string{to_string_view(*encryption_format_)}
             : std::string{"encryption format is not set"};
}

void storage_core::remove_temporary_objects(
    storage_object_name_container &object_names) {
  using remove_object_container = std::vector<std::string>;
  remove_object_container remove_objects;
  for (auto it{std::begin(object_names)}; it != std::end(object_names);) {
    const std::filesystem::path object_name{it->first};
    if (object_name.has_extension() &&
        object_name.extension() == tmp_storage_object_suffix) {
      auto object_node = object_names.extract(it++);
      remove_objects.emplace_back(std::move(object_node.key()));
      logger_->log_format(log_severity::warning,
                          "found temporary storage object '{}' left after "
                          "improper shutdown",
                          object_name.string());
    } else {
      ++it;
    }
  }
  if (remove_objects.empty()) {
    return;
  }

  // for querying-only mode we do not perform any modifying operations - just
  // clear the 'object_names' container and return
  if (construction_mode_ == storage_construction_mode_type::querying_only) {
    return;
  }

  backend_->remove_objects(remove_objects);
  logger_->log_format(log_severity::warning,
                      "removed {} temporary storage object(s) left after "
                      "improper shutdown",
                      remove_objects.size());
}

void storage_core::initialize_storage_encryption(
    const optional_encryption_config &encryption_config) {
  if (encryption_config.has_value()) {
    encryption_format_ = encryption_config->get<"format">();
    if (!is_keyring_initialized()) {
      util::exception_location().raise<std::logic_error>(
          "encryption is enabled but keyring is not initialized");
    }
    const auto &kek_id{encryption_config->get<"kek_id">()};
    if (!keyring_->contains(kek_id)) {
      util::exception_location().raise<std::runtime_error>(
          "keyring does not contain the specified KEK ID");
    }
    active_kek_id_ = kek_id;
    active_data_cipher_ = encryption_config->get<"cipher">();

    // make sure that random file keys (of length that corresponds to the
    // active data cipher) can be encrypted with the active KEK -
    // for instance, if active data cipher is AES-192-CRT (key length 24
    // bytes), then the active KEK cannot be of ECB or CBC mode as these
    // ciphers can only encrypt data of length that is a multiple of the
    // block size (16 bytes)
    const auto &keyring_record{keyring_->get_key(active_kek_id_)};
    if (opensslpp::cipher_context::get_key_size_in_bytes(active_data_cipher_) %
            opensslpp::cipher_context::get_block_size_in_bytes(
                keyring_record.get<"cipher">()) !=
        0U) {
      util::exception_location().raise<std::runtime_error>(
          "active data cipher key length is not compatible with the active "
          "KEK cipher block size");
    }
  }
}

void storage_core::ensure_streaming_mode() const {
  if (construction_mode_ != storage_construction_mode_type::streaming) {
    util::exception_location().raise<std::logic_error>(
        "operation requires storage to be constructed in streaming mode");
  }
}

void storage_core::ensure_purging_mode() const {
  if (construction_mode_ != storage_construction_mode_type::purging) {
    util::exception_location().raise<std::logic_error>(
        "operation requires storage to be constructed in purging mode");
  }
}

[[nodiscard]] open_binlog_status storage_core::open_new_binlog_file_internal(
    const events::composite_binlog_name &binlog_name) {

  auto encryption_record{generate_binlog_encryption_record()};
  // writing the magic binlog footprint only if this is a newly
  // created file; the backend makes it durable before returning, so that
  // the binlog metadata object written below never describes missing data
  write_data_to_stream(events::magic_binlog_payload, encryption_record, 0ULL);

  gtids::optional_gtid_set previous_binlog_gtids{};
  gtids::optional_gtid_set added_binlog_gtids{};
  if (is_in_gtid_replication_mode()) {
    previous_binlog_gtids = get_gtids_unsafe();
    added_binlog_gtids = gtids::gtid_set{};
  }

  const auto ordinal{is_empty_unsafe()
                         ? purge_horizon_
                         : get_current_binlog_record_unsafe().ordinal + 1ULL};
  binlog_records_.emplace_back(
      binlog_name, ordinal, events::magic_binlog_offset,
      std::move(previous_binlog_gtids), std::move(added_binlog_gtids),
      util::ctime_timestamp_range{}, events::seq_no_t{},
      std::move(encryption_record));
  // saving the binlog metadata object is the commit point of creating a new
  // binlog file: if the process is killed before this call completes, the
  // binlog data file created above contains no events and will be removed
  // during the next startup
  save_binlog_metadata(get_current_binlog_record_unsafe());
  try_save_binlog_index();
  return open_binlog_status::created;
}
[[nodiscard]] open_binlog_status
storage_core::open_existing_binlog_file_internal(
    std::uint64_t open_stream_offset) {
  assert(get_flushed_position_unsafe() == open_stream_offset);
  if (open_stream_offset >= events::magic_binlog_offset) {
    return open_stream_offset == events::magic_binlog_offset
               ? open_binlog_status::opened_at_magic_payload_offset
               : open_binlog_status::opened_with_data_present;
  }
  assert(open_stream_offset == 0ULL);

  write_data_to_stream(events::magic_binlog_payload,
                       get_current_binlog_record_unsafe().encryption, 0ULL);
  get_current_binlog_record_unsafe().size = events::magic_binlog_offset;
  return open_binlog_status::opened_empty;
}

void storage_core::save_binlog_index() const {
  std::ostringstream oss;
  for (const auto &record : binlog_records_) {
    std::filesystem::path binlog_path{default_binlog_index_entry_path};
    binlog_path /= record.name.str();
    oss << binlog_path.generic_string() << '\n';
  }
  const auto content{oss.str()};
  backend_->put_object(default_binlog_index_name,
                       util::as_const_byte_span(content));
}

void storage_core::try_save_binlog_index() const {
  // binlog index is a derived object, so failing to update it must not fail
  // the operation that has already been committed via binlog metadata
  // objects - the index will be regenerated during the next startup
  try {
    save_binlog_index();
  } catch (const std::exception &e) {
    logger_->log_format(log_severity::warning, "cannot update binlog index: {}",
                        e.what());
  }
}

void storage_core::load_metadata() {
  const auto metadata_content{backend_->get_object(metadata_name)};
  const storage_metadata metadata{util::as_string_view(metadata_content)};
  replication_mode_ = metadata.root().get<"mode">();
  encryption_format_ = metadata.root().get<"encryption">();
  purge_horizon_ = metadata.root().get<"purge_horizon">();
  if (purge_horizon_ == 0ULL) {
    util::exception_location().raise<std::logic_error>(
        "storage metadata contains an invalid purge horizon");
  }
}

void storage_core::validate_metadata(
    replication_mode_type replication_mode,
    const optional_encryption_format_type &encryption_format) const {
  if (replication_mode != replication_mode_) {
    util::exception_location().raise<std::logic_error>(
        "replication mode provided to initialize storage differs from the one "
        "stored in metadata");
  }

  if (encryption_format_.has_value() && encryption_format.has_value() &&
      *encryption_format_ != *encryption_format) {
    // if both encryption formats (the existing one loaded from the storage
    // metadata file and a new one specified in the configuration file) are
    // present and have different values, then this is an error
    util::exception_location().raise<std::logic_error>(
        "storage encryption format provided to initialize storage differs from "
        "the one stored in metadata");
  }
}

void storage_core::save_metadata() const {
  storage_metadata metadata{};
  metadata.root().get<"mode">() = replication_mode_;
  metadata.root().get<"encryption">() = encryption_format_;
  metadata.root().get<"purge_horizon">() = purge_horizon_;
  const auto content{metadata.str()};
  backend_->put_object(metadata_name, util::as_const_byte_span(content));
}

[[nodiscard]] std::string storage_core::generate_binlog_metadata_name(
    const events::composite_binlog_name &binlog_name) {
  std::string binlog_metadata_name{binlog_name.str()};
  binlog_metadata_name += storage_core::binlog_metadata_extension;
  return binlog_metadata_name;
}

[[nodiscard]] binlog_record storage_core::load_binlog_metadata(
    const events::composite_binlog_name &binlog_name) const {
  const auto content{
      backend_->get_object(generate_binlog_metadata_name(binlog_name))};
  binlog_file_metadata metadata{util::as_string_view(content)};

  const auto &optional_encryption_metadata{metadata.root().get<"encryption">()};
  return binlog_record{
      .name = binlog_name,
      .ordinal = metadata.root().get<"ordinal">(),
      .size = metadata.root().get<"size">(),
      .previous_gtids = metadata.root().get<"previous_gtids">(),
      .added_gtids = metadata.root().get<"added_gtids">(),
      .timestamps = {metadata.root().get<"min_timestamp">(),
                     metadata.root().get<"max_timestamp">()},
      .last_sequence_number = metadata.root().get<"last_sequence_number">(),
      .encryption = optional_encryption_metadata.has_value()
                        ? binlog_encryption_record::from_model(
                              *optional_encryption_metadata)
                        : optional_binlog_encryption_record{}};
}

void storage_core::validate_binlog_metadata(const binlog_record &record) const {
  if (record.ordinal == 0ULL) {
    util::exception_location().raise<std::logic_error>(
        "invalid ordinal in the binlog metadata");
  }
  if (is_in_gtid_replication_mode()) {
    if (!record.previous_gtids.has_value()) {
      util::exception_location().raise<std::logic_error>(
          "missing previous GTID set in the binlog metadata while in GTID "
          "replication "
          "mode");
    }
    if (!record.added_gtids.has_value()) {
      util::exception_location().raise<std::logic_error>(
          "missing added GTID set in the binlog metadata while in GTID "
          "replication "
          "mode");
    }
  } else {
    if (record.previous_gtids.has_value()) {
      util::exception_location().raise<std::logic_error>(
          "found previous GTID set in the binlog metadata while in position "
          "replication mode");
    }
    if (record.added_gtids.has_value()) {
      util::exception_location().raise<std::logic_error>(
          "found added GTID set in the binlog metadata while in position "
          "replication mode");
    }
  }
  // make sure that if the encryption record is present in the binlog
  // metadata, keyring must be initialized and contain the KEK with the
  // ID specified in the encryption record
  if (record.encryption.has_value()) {
    if (!is_keyring_initialized()) {
      util::exception_location().raise<std::logic_error>(
          "found encryption record in the binlog metadata but keyring is not "
          "initialized");
    }
    if (!keyring_->contains(record.encryption->kek_id)) {
      util::exception_location().raise<std::logic_error>(
          "found encryption record in the binlog metadata but keyring does not "
          "contain the specified KEK ID");
    }
  }
}

void storage_core::save_binlog_metadata(const binlog_record &record) const {
  binlog_file_metadata metadata{};
  metadata.root().get<"ordinal">() = record.ordinal;
  metadata.root().get<"size">() = record.size;
  metadata.root().get<"previous_gtids">() = record.previous_gtids;
  metadata.root().get<"added_gtids">() = record.added_gtids;
  metadata.root().get<"min_timestamp">() =
      util::ctime_timestamp{record.timestamps.get_min_timestamp()};
  metadata.root().get<"max_timestamp">() =
      util::ctime_timestamp{record.timestamps.get_max_timestamp()};
  metadata.root().get<"last_sequence_number">() = record.last_sequence_number;
  const auto &record_encryption{record.encryption};
  if (record_encryption.has_value()) {
    metadata.root().get<"encryption">() =
        binlog_encryption_record::to_model(*record_encryption);
  }
  const auto content{metadata.str()};
  backend_->put_object(generate_binlog_metadata_name(record.name),
                       util::as_const_byte_span(content));
}

void storage_core::load_and_reconcile_binlog_set(
    storage_object_name_container &object_names) {
  // binlog metadata objects are the source of truth: a binlog file is
  // considered a part of the storage if and only if its metadata object
  // exists and its ordinal is not below the purge horizon
  auto records{load_binlog_metadata_set(object_names)};
  remove_purged_binlogs(records, object_names);
  std::ranges::sort(records, std::less{}, &binlog_record::ordinal);
  validate_binlog_ordinals(records);
  binlog_records_ = std::move(records);

  // binlog data objects are the secondary source of truth - they are
  // validated against (and, where it is safe, reconciled with) the binlog
  // metadata objects loaded above
  validate_binlog_data_objects(object_names);
  remove_uncommitted_binlog_data_objects(object_names);

  // if we are in GTID replication mode, then we can consider GTIDs from the
  // first binlog metadata as purged GTIDs for the whole storage
  if (!binlog_records_.empty()) {
    const auto &optional_added_gtids{binlog_records_.front().added_gtids};
    if (optional_added_gtids.has_value()) {
      purged_gtids_ = *optional_added_gtids;
    }
  }
}

[[nodiscard]] binlog_record_container storage_core::load_binlog_metadata_set(
    storage_object_name_container &object_names) const {
  binlog_record_container records;
  for (auto object_it{std::begin(object_names)};
       object_it != std::end(object_names);) {
    const std::filesystem::path object_name{object_it->first};
    if (!object_name.has_extension() ||
        object_name.extension() != binlog_metadata_extension) {
      ++object_it;
      continue;
    }
    const auto binlog_name{try_parse_binlog_name(object_name.stem().string())};
    if (!binlog_name.has_value()) {
      // objects with unexpected names are left in 'object_names' and
      // reported by 'remove_uncommitted_binlog_data_objects()'
      ++object_it;
      continue;
    }
    object_it = object_names.erase(object_it);

    binlog_record loaded_binlog_metadata{};
    try {
      loaded_binlog_metadata = load_binlog_metadata(*binlog_name);
    } catch (const std::exception &) {
      if (construction_mode_ == storage_construction_mode_type::querying_only) {
        // in the querying_only mode we just skip invalid metadata and the
        // corresponding binlog file - this allows to query an otherwise
        // unusable storage and retrieve information about valid binlog files
        // from it, which can be useful for debugging / forensics purposes
        continue;
      }
      throw;
    }
    validate_binlog_metadata(loaded_binlog_metadata);
    records.push_back(std::move(loaded_binlog_metadata));
  }
  return records;
}

void storage_core::remove_purged_binlogs(
    binlog_record_container &records,
    storage_object_name_container &object_names) {
  // binlog files with ordinals below the purge horizon are left after a
  // purge operation that was committed but did not finish removing its
  // victims (the process was killed or object removal failed)
  const auto purged_records{
      std::ranges::partition(records, [this](const binlog_record &record) {
        return record.ordinal >= purge_horizon_;
      })};
  if (purged_records.empty()) {
    return;
  }

  std::vector<std::string> data_object_names;
  std::vector<std::string> metadata_object_names;
  for (const auto &record : purged_records) {
    auto binlog_file_name{record.name.str()};
    logger_->log_format(log_severity::warning,
                        "found binlog file '{}' below the purge horizon left "
                        "after an interrupted purge",
                        binlog_file_name);
    // the data object may have already been removed by the interrupted
    // purge operation
    const auto data_object_it{object_names.find(binlog_file_name)};
    if (data_object_it != std::end(object_names)) {
      object_names.erase(data_object_it);
      data_object_names.emplace_back(std::move(binlog_file_name));
    }
    metadata_object_names.emplace_back(
        generate_binlog_metadata_name(record.name));
  }
  const auto number_of_purged_binlogs{std::size(purged_records)};
  records.erase(std::begin(purged_records), std::end(purged_records));

  // for querying-only mode we do not perform any modifying operations
  if (construction_mode_ == storage_construction_mode_type::querying_only) {
    return;
  }

  // the same order as in 'purge_binlogs()': data objects go first so that a
  // leftover data object can never outlive its metadata object
  if (!data_object_names.empty()) {
    backend_->remove_objects(data_object_names);
  }
  backend_->remove_objects(metadata_object_names);
  logger_->log_format(log_severity::warning,
                      "removed {} binlog file(s) below the purge horizon left "
                      "after an interrupted purge",
                      number_of_purged_binlogs);
}

void storage_core::validate_binlog_ordinals(
    const binlog_record_container &records) const {
  // in the querying_only mode we allow gaps in the sequence of binlog files
  if (construction_mode_ == storage_construction_mode_type::querying_only) {
    return;
  }

  // 'records' is expected to be sorted by ordinal here, so any pair of
  // adjacent records with non-consecutive ordinals indicates either a
  // duplicate or a missing binlog metadata object
  const auto gap_it{std::ranges::adjacent_find(
      records, [](const binlog_record &previous, const binlog_record &next) {
        return next.ordinal != previous.ordinal + 1ULL;
      })};
  if (gap_it == std::cend(records)) {
    return;
  }
  if (std::next(gap_it)->ordinal == gap_it->ordinal) {
    util::exception_location().raise<std::logic_error>(
        "storage contains binlog metadata with duplicate ordinals");
  }
  util::exception_location().raise<std::logic_error>(
      "storage contains binlog metadata with non-contiguous ordinals");
}

void storage_core::validate_binlog_data_objects(
    storage_object_name_container &object_names) {
  auto record_it{std::begin(binlog_records_)};
  while (record_it != std::end(binlog_records_)) {
    const auto binlog_file_name{record_it->name.str()};
    const auto data_object_it{object_names.find(binlog_file_name)};
    if (data_object_it == std::end(object_names)) {
      if (construction_mode_ == storage_construction_mode_type::querying_only) {
        // in the querying_only mode we just skip binlog files without data
        record_it = binlog_records_.erase(record_it);
        continue;
      }
      util::exception_location().raise<std::logic_error>(
          "storage contains binlog metadata for a non-existing binlog");
    }
    const auto actual_binlog_file_size{data_object_it->second};
    object_names.erase(data_object_it);

    // validating binlog size from the metadata only makes sense if we are not
    // in the querying_only mode
    if (construction_mode_ != storage_construction_mode_type::querying_only &&
        record_it->size != actual_binlog_file_size) {
      // in case when Binlog Server process was not properly shut down
      // there is a chance that there will be mismatch between the actual
      // binlog data file size and the 'size' field in the binlog metadata

      // if this mismatch was found in the most recent binlog file, we can
      // perform automatic recovery (truncating binlog data file content to
      // the size from the metadata)
      if (std::next(record_it) != std::end(binlog_records_)) {
        util::exception_location().raise<std::logic_error>(
            "size from the binlog metadata does not match the actual binlog "
            "size");
      }
      if (record_it->size > actual_binlog_file_size) {
        util::exception_location().raise<std::logic_error>(
            "cannot perform recovery - size from the binlog metadata is "
            "bigger than the actual binlog file size");
      }
      // only the streaming process owns the most recent binlog file - in
      // the purging mode the extra data may belong to a checkpoint that is
      // being committed by a concurrent streaming process right now
      if (construction_mode_ == storage_construction_mode_type::streaming) {
        backend_->resize_object(binlog_file_name, record_it->size);
        logger_->log_format(log_severity::warning,
                            "recovered binlog file '{}' by truncating it to "
                            "the size from the metadata",
                            binlog_file_name);
      }
    }
    ++record_it;
  }
}

void storage_core::remove_uncommitted_binlog_data_objects(
    const storage_object_name_container &object_names) {
  // in the querying_only mode we allow any extra objects in the storage
  if (object_names.empty() ||
      construction_mode_ == storage_construction_mode_type::querying_only) {
    return;
  }

  // at this point 'object_names' contains only objects not referenced by
  // any binlog metadata object - the only legitimate case is a binlog data
  // file whose creation was interrupted before its metadata object (the
  // commit point) was saved, in which case it contains no events
  std::vector<std::string> uncommitted_object_names;
  uncommitted_object_names.reserve(std::size(object_names));
  for (const auto &[object_name, object_size] : object_names) {
    if (!try_parse_binlog_name(object_name).has_value()) {
      util::exception_location().raise<std::logic_error>(
          "storage contains an object with an unexpected name");
    }
    if (object_size > events::magic_binlog_offset) {
      util::exception_location().raise<std::logic_error>(
          "storage contains a binlog data file that has no metadata");
    }
    uncommitted_object_names.emplace_back(object_name);
  }

  // only the streaming process creates binlog files - in the purging mode
  // these objects may belong to a binlog file that is being created by a
  // concurrent streaming process right now
  if (construction_mode_ != storage_construction_mode_type::streaming) {
    return;
  }

  for (const auto &object_name : uncommitted_object_names) {
    logger_->log_format(log_severity::warning,
                        "found binlog data file '{}' left after an "
                        "interrupted binlog file creation (it has no "
                        "metadata and contains no events)",
                        object_name);
  }
  backend_->remove_objects(uncommitted_object_names);
  logger_->log_format(log_severity::warning,
                      "removed {} binlog data file(s) left after an "
                      "interrupted binlog file creation",
                      std::size(uncommitted_object_names));
}

[[nodiscard]] optional_binlog_encryption_record
storage_core::generate_binlog_encryption_record() const {
  if (!has_active_kek()) {
    return std::nullopt;
  }

  // we identify the KEK record in the keyring by the active KEK ID,
  // specified in the main configuration file
  // ('<storage.encryption.kek_id>' parameter)
  const auto &keyring_record{keyring_->get_key(active_kek_id_)};

  // identifying the the cipher name and the key data from the
  // keyring record - this data will be used to encrypt random file
  // keys generated for new binlog data files
  const auto &kek_cipher{keyring_record.get<"cipher">()};
  const auto &kek{keyring_record.get<"data_hex">().get_data()};

  // identify the size of the IV that will be used for file key
  // encryption based on the KEK cipher; if the KEK cipher is in ECB mode, then
  // the IV is not used and its size will be 0
  const auto iv_size_for_file_key_encryption{
      opensslpp::cipher_context::get_iv_size_in_bytes(kek_cipher)};
  util::optional_hex_value_storage iv_for_file_key_encryption{};
  util::const_byte_span iv_for_file_key_encryption_v{};
  if (iv_size_for_file_key_encryption != 0U) {
    // generating random IV for file key encryption
    iv_for_file_key_encryption.emplace(iv_size_for_file_key_encryption);
    opensslpp::crypto_rng::generate(*iv_for_file_key_encryption);
    iv_for_file_key_encryption_v = *iv_for_file_key_encryption;
  }

  // identify the size of the file key based on the active data cipher
  const auto file_key_size{
      opensslpp::cipher_context::get_key_size_in_bytes(active_data_cipher_)};

  // generating random file key
  util::hex_value_storage file_key{file_key_size};
  opensslpp::crypto_rng::generate(file_key);

  // creating an encryption context with the KEK cipher, the KEK, and
  // the IV for file key encryption
  opensslpp::cipher_context file_key_encryption_context{
      opensslpp::cipher_context_operation_type::encryption, kek_cipher, kek,
      iv_for_file_key_encryption_v};

  // identify the size of the file key encryption tag from the encryption
  // (should be non-zero only for GCM modes)
  const auto file_key_encryption_tag_size{
      file_key_encryption_context.get_tag_size_in_bytes()};
  // provisioning the optional storage for the file key encryption tag
  util::optional_hex_value_storage tag_of_file_key_encryption{};
  util::byte_span tag_of_file_key_encryption_v{};
  if (file_key_encryption_tag_size != 0U) {
    tag_of_file_key_encryption.emplace(file_key_encryption_tag_size);
    tag_of_file_key_encryption_v = *tag_of_file_key_encryption;
  }

  // performing the file key encryption and finalizing the tag (if any)
  util::hex_value_storage file_key_encrypted_with_kek{file_key_size};
  file_key_encryption_context.update(file_key, file_key_encrypted_with_kek);
  file_key_encryption_context.finalize(tag_of_file_key_encryption_v);

  // identifying the size of the IV that will be used for data encryption based
  // on the active data cipher
  const auto iv_length_for_data_encryption{
      opensslpp::cipher_context::get_iv_size_in_bytes(active_data_cipher_)};
  // generating random IV for file data encryption
  util::hex_value_storage iv_for_data_encryption{iv_length_for_data_encryption};
  opensslpp::crypto_rng::generate(iv_for_data_encryption);

  // the tag of data encryption will be generated during the actual data
  // encryption
  binlog_encryption_record encryption_record{
      .kek_id = active_kek_id_,
      .file_key_encrypted_with_kek = file_key_encrypted_with_kek,
      .iv_for_file_key_encryption = iv_for_file_key_encryption,
      .tag_of_file_key_encryption = tag_of_file_key_encryption,
      .data_cipher = active_data_cipher_,
      .iv_for_data_encryption = iv_for_data_encryption,
      .tag_of_data_encryption = {}};

  return encryption_record;
}

void storage_core::write_data_to_stream(
    util::const_byte_span data,
    const optional_binlog_encryption_record &encryption_record,
    std::uint64_t offset) {
  if (!encryption_record.has_value()) {
    // an early return when no encryption is needed
    backend_->write_data_to_stream(data);
    return;
  }

  // as for security reasons our intent is to not store file keys in plaintext
  // permanently, we need to decrypt the file key with the KEK before we can
  // use it for data encryption.

  const auto &keyring_record{keyring_->get_key(encryption_record->kek_id)};

  const auto &kek_cipher{keyring_record.get<"cipher">()};
  const auto &kek{keyring_record.get<"data_hex">().get_data()};

  util::const_byte_span iv_for_file_key_encryption_v{};
  if (encryption_record->iv_for_file_key_encryption.has_value()) {
    iv_for_file_key_encryption_v =
        *encryption_record->iv_for_file_key_encryption;
  };
  util::const_byte_span tag_of_file_key_encryption_v{};
  if (encryption_record->tag_of_file_key_encryption.has_value()) {
    tag_of_file_key_encryption_v =
        *encryption_record->tag_of_file_key_encryption;
  }

  // creating a context for the file key decryption
  opensslpp::cipher_context file_key_decryption_context{
      opensslpp::cipher_context_operation_type::decryption, kek_cipher, kek,
      iv_for_file_key_encryption_v, tag_of_file_key_encryption_v};
  util::hex_value_storage file_key_decrypted{
      std::size(encryption_record->file_key_encrypted_with_kek)};
  file_key_decryption_context.update(
      encryption_record->file_key_encrypted_with_kek, file_key_decrypted);
  file_key_decryption_context.finalize();

  // creating an context for data encryption with the data cipher, the file
  // key (decrypted previously), and the IV for data encryption

  auto data_encryption_context{opensslpp::cipher_context::create_with_offset(
      offset, opensslpp::cipher_context_operation_type::encryption,
      encryption_record->data_cipher, file_key_decrypted,
      encryption_record->iv_for_data_encryption)};

  util::optional_hex_value_storage tag_of_data_encryption{};
  util::byte_span tag_of_data_encryption_v{};
  const auto data_encryption_tag_size{
      data_encryption_context.get_tag_size_in_bytes()};
  if (data_encryption_tag_size != 0U) {
    tag_of_data_encryption.emplace(data_encryption_tag_size);
    tag_of_data_encryption_v = *tag_of_data_encryption;
  }

  util::hex_value_storage encrypted_data{std::size(data)};
  data_encryption_context.update(data, encrypted_data);
  data_encryption_context.finalize(tag_of_data_encryption_v);

  backend_->write_data_to_stream(encrypted_data);

  // TODO: update file data encryption tag here, if one day we decide to
  //       support GCM mode for file data encryption
}

} // namespace binsrv
