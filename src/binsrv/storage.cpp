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

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "binsrv/basic_keyring.hpp"
#include "binsrv/basic_storage_backend.hpp"
#include "binsrv/binlog_file_metadata.hpp"
#include "binsrv/encryption_format_type.hpp"
#include "binsrv/keyring_config.hpp"
#include "binsrv/keyring_factory.hpp"
#include "binsrv/keyring_record.hpp"
#include "binsrv/replication_mode_type.hpp"
#include "binsrv/storage_backend_factory.hpp"
#include "binsrv/storage_config.hpp"
#include "binsrv/storage_metadata.hpp"

#include "binsrv/events/common_types.hpp"
#include "binsrv/events/composite_binlog_name.hpp"
#include "binsrv/events/protocol_traits_fwd.hpp"

#include "binsrv/gtids/gtid.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "binsrv/models/binlog_file_encryption_record.hpp"

#include "opensslpp/cipher_context.hpp"
#include "opensslpp/crypto_rng.hpp"

#include "util/byte_span.hpp"
#include "util/ctime_timestamp.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

[[nodiscard]] models::binlog_file_encryption_record
storage::binlog_encryption_record::to_model(
    const storage::binlog_encryption_record &record) {
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

[[nodiscard]] storage::binlog_encryption_record
storage::binlog_encryption_record::from_model(
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

storage::storage(const optional_keyring_config &keyring_config,
                 const storage_config &config,
                 storage_construction_mode_type construction_mode,
                 replication_mode_type replication_mode)
    : construction_mode_{construction_mode}, backend_{},
      replication_mode_{replication_mode} {
  const auto &checkpoint_size_opt{config.get<"checkpoint_size">()};
  if (checkpoint_size_opt.has_value()) {
    checkpoint_size_bytes_ = checkpoint_size_opt->get_value();
  }

  const auto &checkpoint_interval_opt{config.get<"checkpoint_interval">()};
  if (checkpoint_interval_opt.has_value()) {
    checkpoint_interval_seconds_ =
        std::chrono::seconds{checkpoint_interval_opt->get_value()};
  }

  if (keyring_config.has_value()) {
    keyring_ = keyring_factory::create(keyring_config->get<"uri">());
  }
  const auto &encryption_config{config.get<"encryption">()};
  initialize_storage_encryption(encryption_config);

  backend_ = storage_backend_factory::create(config);

  auto storage_objects{backend_->list_objects()};
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

  // if after metadata erasure 'storage_objects' is empty, then this mean
  // that it has only metadata in it that passes validation and we can
  // consider it as an initialized empty storage, so just return
  if (storage_objects.empty()) {
    return;
  }

  const auto binlog_index_it{storage_objects.find(default_binlog_index_name)};
  if (binlog_index_it == std::cend(storage_objects)) {
    // as binlog index file is created after the very first binlog data file
    // and its metadata file are created, the concurrent query-only operation
    // may intrude exactly between these two steps, so for query-only operations
    // we should not consider the absence of binlog index file as an error
    if (construction_mode == storage_construction_mode_type::querying_only) {
      return;
    }
    util::exception_location().raise<std::logic_error>(
        "storage is not empty but does not contain binlog index");
  }
  storage_objects.erase(binlog_index_it);

  // extracting all binlog file metadata files into a separate container
  storage_object_name_container storage_metadata_objects;
  for (auto storage_object_it{std::cbegin(storage_objects)};
       storage_object_it != std::cend(storage_objects);) {
    const std::filesystem::path object_name{storage_object_it->first};
    if (object_name.has_extension() &&
        object_name.extension() == binlog_metadata_extension) {
      auto object_node = storage_objects.extract(storage_object_it++);
      storage_metadata_objects.insert(std::move(object_node));
    } else {
      ++storage_object_it;
    }
  }
  load_binlog_index();
  validate_binlog_index(storage_objects);

  load_and_validate_binlog_metadata_set(storage_objects,
                                        storage_metadata_objects);
  assert(!binlog_records_.front().added_gtids.has_value() ||
         purged_gtids_ == binlog_records_.front().added_gtids);
}

storage::~storage() {
  if (construction_mode_ == storage_construction_mode_type::streaming) {
    // bugprone-empty-catch should not be that strict in destructors
    try {
      flush_event_buffer();
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
  }
}

void storage::set_purged_gtids(const gtids::gtid_set &purged_gtids) {
  if (!is_in_gtid_replication_mode()) {
    util::exception_location().raise<std::logic_error>(
        "cannot set purged GTIDs in position-based replication mode");
  }
  if (!is_empty()) {
    util::exception_location().raise<std::logic_error>(
        "cannot set purged GTIDs in a non-empty storage");
  }
  purged_gtids_ = purged_gtids;
}

[[nodiscard]] std::string storage::get_backend_description() const {
  return backend_->get_description();
}

[[nodiscard]] bool storage::is_in_gtid_replication_mode() const noexcept {
  return replication_mode_ == replication_mode_type::gtid;
}

[[nodiscard]] bool storage::is_binlog_open() const noexcept {
  return backend_->is_stream_open();
}

[[nodiscard]] open_binlog_status
storage::open_binlog(const events::composite_binlog_name &binlog_name) {
  ensure_streaming_mode();

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
    if (binlog_name != get_current_binlog_name()) {
      util::exception_location().raise<std::logic_error>(
          "cannot open an existing binlog that is not the latest one for "
          "append");
    }
    if (get_current_position() == 0ULL) {
      util::exception_location().raise<std::logic_error>(
          "invalid position set when opening an existing binlog");
    }
  }

  const auto mode{binlog_exists ? storage_backend_open_stream_mode::append
                                : storage_backend_open_stream_mode::create};
  const auto open_stream_offset{backend_->open_stream(binlog_name.str(), mode)};

  if (binlog_exists) {
    result = open_existing_binlog_file_internal(open_stream_offset);
    ready_to_flush_last_sequence_number_ =
        binlog_records_.back().last_sequence_number;
    incomplete_transaction_last_sequence_number_ =
        ready_to_flush_last_sequence_number_;
  } else {
    result = open_new_binlog_file_internal(binlog_name);
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

  backend_->close_stream();
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

[[nodiscard]] std::pair<storage::binlog_record_container, std::string>
storage::purge_binlogs(const events::composite_binlog_name &target) {
  ensure_purging_mode();

  if (is_empty()) {
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

  // step 2: rewrite the binlog index from the surviving records left
  // in 'binlog_records_' after step 1 (always non-empty thanks to the
  // tail-refusal guard above). 'save_binlog_index' goes through the
  // backend's atomic-overwrite 'put_object', so from this point on
  // the purge is considered committed - any subsequent failure
  // leaves the storage in an inconsistent state (leftover payload /
  // metadata files no longer referenced by the index) that the
  // constructor's existing validators will refuse to open on next
  // startup.
  save_binlog_index();

  // step 3: best-effort removal of the victim payload + metadata
  // objects; any failure here is intentionally swallowed - the index
  // has already been committed and reporting a "file could not be
  // removed" error to the caller would falsely suggest that the
  // purge itself failed; the resulting leftovers will trip the
  // constructor's validators on next startup.
  // We materialise the (metadata + payload) names for every victim
  // into a single batch and hand it to 'basic_storage_backend::
  // remove_objects', which runs the backend's durability barrier
  // exactly once at the end of the batch - so the whole batch
  // amortises to a single fsync(2) on the local filesystem backend
  // (and a no-op on S3) instead of O(N) syncs.
  std::vector<std::string> victim_object_names;
  victim_object_names.reserve(std::size(removed_records) * 2U);
  for (const auto &victim : removed_records) {
    victim_object_names.emplace_back(
        generate_binlog_metadata_name(victim.name));
    victim_object_names.emplace_back(victim.name.str());
  }
  std::string cleanup_warning_message;
  try {
    backend_->remove_objects(victim_object_names);
  } catch (const std::exception &e) {
    // 'remove_objects' re-raises the first per-name failure (if any)
    // after running the durability barrier; we do not propagate it
    // to the caller because the index has already been committed
    // and any leftover payload/metadata files will be picked up by
    // the constructor's validators on the next startup. We just
    // capture the underlying message so the caller can surface it
    // under a 'warning' status in the JSON response.
    cleanup_warning_message = e.what();
  }

  return {std::move(removed_records), std::move(cleanup_warning_message)};
}

[[nodiscard]] std::string storage::get_binlog_uri(
    const events::composite_binlog_name &binlog_name) const {
  return backend_->get_object_uri(binlog_name.str());
}

[[nodiscard]] std::string storage::get_keyring_description() const {
  return is_keyring_initialized() ? keyring_->get_description()
                                  : "keyring is not initialized";
}

[[nodiscard]] std::string storage::get_active_kek_description() const {
  return has_active_kek() ? keyring_->get_key(active_kek_id_).get_description()
                          : "active KEK is not set";
}

[[nodiscard]] std::string storage::get_encryption_format_description() const {
  return encryption_format_.has_value()
             ? std::string{to_string_view(*encryption_format_)}
             : std::string{"encryption format is not set"};
}

void storage::initialize_storage_encryption(
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

void storage::ensure_streaming_mode() const {
  if (construction_mode_ != storage_construction_mode_type::streaming) {
    util::exception_location().raise<std::logic_error>(
        "operation requires storage to be constructed in streaming mode");
  }
}

void storage::ensure_purging_mode() const {
  if (construction_mode_ != storage_construction_mode_type::purging) {
    util::exception_location().raise<std::logic_error>(
        "operation requires storage to be constructed in purging mode");
  }
}

void storage::update_last_checkpoint_info() {
  if (size_checkpointing_enabled()) {
    last_checkpoint_position_ = get_current_position();
  }
  if (interval_checkpointing_enabled()) {
    last_checkpoint_timestamp_ = std::chrono::steady_clock::now();
  }
}

[[nodiscard]] open_binlog_status storage::open_new_binlog_file_internal(
    const events::composite_binlog_name &binlog_name) {

  auto encryption_record{generate_binlog_encryption_record()};
  // writing the magic binlog footprint only if this is a newly
  // created file
  write_data_to_stream(events::magic_binlog_payload, encryption_record, 0ULL);

  gtids::optional_gtid_set previous_binlog_gtids{};
  gtids::optional_gtid_set added_binlog_gtids{};
  if (is_in_gtid_replication_mode()) {
    previous_binlog_gtids = get_gtids();
    added_binlog_gtids = gtids::gtid_set{};
  }

  binlog_records_.emplace_back(
      binlog_name, events::magic_binlog_offset,
      std::move(previous_binlog_gtids), std::move(added_binlog_gtids),
      util::ctime_timestamp_range{}, events::seq_no_t{},
      std::move(encryption_record));
  save_binlog_metadata(get_current_binlog_record());
  save_binlog_index();
  return open_binlog_status::created;
}
[[nodiscard]] open_binlog_status
storage::open_existing_binlog_file_internal(std::uint64_t open_stream_offset) {
  assert(get_current_position() == open_stream_offset);
  if (open_stream_offset >= events::magic_binlog_offset) {
    return open_stream_offset == events::magic_binlog_offset
               ? open_binlog_status::opened_at_magic_payload_offset
               : open_binlog_status::opened_with_data_present;
  }
  assert(open_stream_offset == 0ULL);

  write_data_to_stream(events::magic_binlog_payload,
                       get_current_binlog_record().encryption, 0ULL);
  get_current_binlog_record().size = events::magic_binlog_offset;
  return open_binlog_status::opened_empty;
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
  write_data_to_stream(transactions_data,
                       get_current_binlog_record().encryption,
                       get_current_binlog_record().size);
  get_current_binlog_record().size +=
      last_transaction_boundary_position_in_event_buffer_;
  if (is_in_gtid_replication_mode()) {
    auto &optional_added_gtids{get_current_binlog_record().added_gtids};
    if (optional_added_gtids.has_value()) {
      *optional_added_gtids += gtids_in_event_buffer_;
    }
  }
  get_current_binlog_record().timestamps.add_range(ready_to_flush_timestamps_);
  get_current_binlog_record().last_sequence_number =
      ready_to_flush_last_sequence_number_;

  save_binlog_metadata(get_current_binlog_record());

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

void storage::load_binlog_index() {
  const auto index_content{backend_->get_object(default_binlog_index_name)};
  // opening in text mode
  std::istringstream index_iss{index_content};
  std::string current_line;
  while (std::getline(index_iss, current_line)) {
    if (current_line.empty()) {
      continue;
    }
    const std::filesystem::path current_binlog_path{current_line};
    if (current_binlog_path.parent_path() != default_binlog_index_entry_path) {
      util::exception_location().raise<std::logic_error>(
          "binlog index contains an entry that has an invalid path");
    }
    auto current_binlog_name{current_binlog_path.filename().string()};

    if (current_binlog_name == default_binlog_index_name) {
      util::exception_location().raise<std::logic_error>(
          "binlog index contains a reference to the binlog index name");
    }
    const auto current_binlog_name_parsed{
        events::composite_binlog_name::parse(current_binlog_name)};
    if (std::ranges::find(std::as_const(binlog_records_),
                          current_binlog_name_parsed,
                          &binlog_record::name) != std::cend(binlog_records_)) {
      util::exception_location().raise<std::logic_error>(
          "binlog index contains a duplicate entry");
    }
    gtids::optional_gtid_set previous_binlog_gtids{};
    gtids::optional_gtid_set added_binlog_gtids{};
    if (is_in_gtid_replication_mode()) {
      previous_binlog_gtids = gtids::gtid_set{};
      added_binlog_gtids = gtids::gtid_set{};
    }
    binlog_records_.emplace_back(
        current_binlog_name_parsed, 0ULL, std::move(previous_binlog_gtids),
        std::move(added_binlog_gtids), util::ctime_timestamp_range{});
  }
}

void storage::validate_binlog_index(
    const storage_object_name_container &object_names) const {
  // in the querying_only mode we allow discrepancies between the binlog index
  // and the actual objects in the storage
  if (construction_mode_ == storage_construction_mode_type::querying_only) {
    return;
  }

  for (auto const &record : binlog_records_) {
    if (!object_names.contains(record.name.str())) {
      util::exception_location().raise<std::logic_error>(
          "binlog index contains a reference to a non-existing object");
    }
  }

  if (std::size(object_names) != std::size(binlog_records_)) {
    util::exception_location().raise<std::logic_error>(
        "storage contains an object that is not "
        "referenced in the binlog index");
  }

  // TODO: add integrity checks (parsing + checksumming) for the binlog
  //       files in the index
}

void storage::save_binlog_index() const {
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

void storage::load_metadata() {
  const auto metadata_content{backend_->get_object(metadata_name)};
  const storage_metadata metadata{metadata_content};
  replication_mode_ = metadata.root().get<"mode">();
  encryption_format_ = metadata.root().get<"encryption">();
}

void storage::validate_metadata(
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

void storage::save_metadata() const {
  storage_metadata metadata{};
  metadata.root().get<"mode">() = replication_mode_;
  metadata.root().get<"encryption">() = encryption_format_;
  const auto content{metadata.str()};
  backend_->put_object(metadata_name, util::as_const_byte_span(content));
}

[[nodiscard]] std::string storage::generate_binlog_metadata_name(
    const events::composite_binlog_name &binlog_name) {
  std::string binlog_metadata_name{binlog_name.str()};
  binlog_metadata_name += storage::binlog_metadata_extension;
  return binlog_metadata_name;
}

[[nodiscard]] storage::binlog_record storage::load_binlog_metadata(
    const events::composite_binlog_name &binlog_name) const {
  const auto content{
      backend_->get_object(generate_binlog_metadata_name(binlog_name))};
  binlog_file_metadata metadata{content};

  const auto &optional_encryption_metadata{metadata.root().get<"encryption">()};
  return binlog_record{
      .name = binlog_name,
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

void storage::validate_binlog_metadata(const binlog_record &record) const {
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

void storage::save_binlog_metadata(const binlog_record &record) const {
  binlog_file_metadata metadata{};
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

void storage::load_and_validate_binlog_metadata_set(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const storage_object_name_container &object_names,
    const storage_object_name_container &object_metadata_names) {
  auto record_it{std::begin(binlog_records_)};
  while (record_it != std::end(binlog_records_)) {
    storage::binlog_record loaded_binlog_metadata{};
    try {
      const auto binlog_metadata_name{
          generate_binlog_metadata_name(record_it->name)};
      if (!object_metadata_names.contains(binlog_metadata_name)) {
        util::exception_location().raise<std::logic_error>(
            "missing metadata for a binlog listed in the binlog index");
      }
      loaded_binlog_metadata = load_binlog_metadata(record_it->name);
    } catch (const std::exception &) {
      if (construction_mode_ == storage_construction_mode_type::querying_only) {
        // in the querying_only mode we just skip invalid metadata and the
        // corresponding binlog file - this allows to query an otherwise
        // unusable storage and retrieve information about valid binlog files
        // from it, which can be useful for debugging / forensics purposes
        record_it = binlog_records_.erase(record_it);
        continue;
      }
      throw;
    }
    validate_binlog_metadata(loaded_binlog_metadata);
    // validating binlog size from the metadata only makes sense if we are not
    // in the querying_only mode
    if (construction_mode_ != storage_construction_mode_type::querying_only) {
      // validating that the size stored in the metadata matches the actual size
      if (loaded_binlog_metadata.size !=
          object_names.at(record_it->name.str())) {
        util::exception_location().raise<std::logic_error>(
            "size from the binlog metadata does not match the actual binlog "
            "size");
      }
    }
    *record_it = std::move(loaded_binlog_metadata);
    ++record_it;
  }
  // after this loop position_ and gtids_ should store the values from the last
  // binlog file metadata

  if (construction_mode_ != storage_construction_mode_type::querying_only) {
    if (std::size(object_metadata_names) != std::size(binlog_records_)) {
      util::exception_location().raise<std::logic_error>(
          "found metadata for a non-existing binlog");
    }
  }

  // if we are in GTID replication mode, then we can consider GTIDs from the
  // first binlog metadata as purged GTIDs for the whole storage
  const auto &optional_added_gtids{binlog_records_.front().added_gtids};
  if (optional_added_gtids.has_value()) {
    purged_gtids_ = *optional_added_gtids;
  }
}

[[nodiscard]] storage::optional_binlog_encryption_record
storage::generate_binlog_encryption_record() const {
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

void storage::write_data_to_stream(
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
