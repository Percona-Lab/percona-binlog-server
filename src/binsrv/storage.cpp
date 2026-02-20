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
#include <filesystem>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "binsrv/basic_storage_backend.hpp"
#include "binsrv/binlog_file_metadata.hpp"
#include "binsrv/ctime_timestamp.hpp"
#include "binsrv/replication_mode_type.hpp"
#include "binsrv/storage_backend_factory.hpp"
#include "binsrv/storage_config.hpp"
#include "binsrv/storage_metadata.hpp"

#include "binsrv/events/protocol_traits_fwd.hpp"

#include "binsrv/gtids/gtid.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "util/byte_span.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

storage::storage(const storage_config &config,
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

  backend_ = storage_backend_factory::create(config);

  auto storage_objects{backend_->list_objects()};
  if (storage_objects.empty()) {
    // initialized on a new / empty storage - just save metadata and return
    if (construction_mode_ == storage_construction_mode_type::streaming) {
      save_metadata();
    }
    return;
  }

  const auto metadata_it{storage_objects.find(metadata_name)};
  if (metadata_it == std::end(storage_objects)) {
    util::exception_location().raise<std::logic_error>(
        "storage is not empty but does not contain metadata");
  }
  storage_objects.erase(metadata_it);

  load_metadata();
  validate_metadata(replication_mode);

  const auto binlog_index_it{storage_objects.find(default_binlog_index_name)};
  if (binlog_index_it == std::end(storage_objects)) {
    util::exception_location().raise<std::logic_error>(
        "storage is not empty but does not contain binlog index");
  }
  storage_objects.erase(binlog_index_it);

  // extracting all binlog file metadata files into a separate container
  storage_object_name_container storage_metadata_objects;
  for (auto storage_object_it{std::begin(storage_objects)};
       storage_object_it != std::end(storage_objects);) {
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

[[nodiscard]] std::string storage::get_backend_description() const {
  return backend_->get_description();
}

[[nodiscard]] bool storage::is_in_gtid_replication_mode() const noexcept {
  return replication_mode_ == replication_mode_type::gtid;
}

[[nodiscard]] bool
storage::check_binlog_name(std::string_view binlog_name) noexcept {
  // TODO: parse binlog name into "base name" and "rotation number"
  //       e.g. "binlog.000001" -> ("binlog", 1)

  // currently checking only that the name does not include a filesystem
  // separator
  return binlog_name.find(std::filesystem::path::preferred_separator) ==
         std::string_view::npos;
}

[[nodiscard]] bool storage::is_binlog_open() const noexcept {
  return backend_->is_stream_open();
}

[[nodiscard]] open_binlog_status
storage::open_binlog(std::string_view binlog_name) {
  ensure_streaming_mode();

  auto result{open_binlog_status::opened_with_data_present};

  if (!check_binlog_name(binlog_name)) {
    util::exception_location().raise<std::logic_error>(
        "cannot create a binlog with invalid name");
  }

  // here we either create a new binlog file if its name is not presen in the
  // "binlog_records_", or we open an existing one and append to it, in which
  // case we need to make sure that the current position is properly set
  const bool binlog_exists{
      std::ranges::find(binlog_records_, binlog_name, &binlog_record::name) !=
      std::end(binlog_records_)};

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
  const auto open_stream_offset{backend_->open_stream(binlog_name, mode)};

  if (!binlog_exists) {
    // writing the magic binlog footprint only if this is a newly
    // created file
    backend_->write_data_to_stream(events::magic_binlog_payload);

    gtids::optional_gtid_set previous_binlog_gtids{};
    gtids::optional_gtid_set added_binlog_gtids{};
    if (is_in_gtid_replication_mode()) {
      previous_binlog_gtids = get_gtids();
      added_binlog_gtids = gtids::gtid_set{};
    }

    binlog_records_.emplace_back(
        std::string{binlog_name}, events::magic_binlog_offset,
        std::move(previous_binlog_gtids), std::move(added_binlog_gtids),
        ctime_timestamp_range{});
    save_binlog_metadata(get_current_binlog_record());
    save_binlog_index();
    result = open_binlog_status::created;
  } else {
    assert(get_current_position() == open_stream_offset);
    if (open_stream_offset == 0ULL) {
      backend_->write_data_to_stream(events::magic_binlog_payload);
      get_current_binlog_record().size = events::magic_binlog_offset;
      result = open_binlog_status::opened_empty;
    } else if (open_stream_offset == events::magic_binlog_offset) {
      result = open_binlog_status::opened_at_magic_paylod_offset;
    } else {
      // position is beyond magic payload offset
      assert(open_stream_offset > events::magic_binlog_offset);
      result = open_binlog_status::opened_with_data_present;
    }
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
                          const ctime_timestamp &event_timestamp) {
  ensure_streaming_mode();

  event_buffer_.insert(std::end(event_buffer_), std::cbegin(event_data),
                       std::cend(event_data));
  incomplete_transaction_timestamps_.add_timestamp(event_timestamp);

  if (at_transaction_boundary) {
    last_transaction_boundary_position_in_event_buffer_ =
        std::size(event_buffer_);
    if (is_in_gtid_replication_mode() && !transaction_gtid.is_empty()) {
      gtids_in_event_buffer_ += transaction_gtid;
    }
    ready_to_flush_timestamps_.add_range(incomplete_transaction_timestamps_);
    incomplete_transaction_timestamps_.clear();
  }

  // now we are writing data from the event buffer to the storage backend if
  // event buffer has some data in it that can be considered a complete
  // transaction and a checkpoint event (either size-based or time-based)
  // occurred or we are processing the very last event in the binlog file

  if (has_event_data_to_flush()) {
    const auto ready_to_flush_position{get_ready_to_flush_position()};
    const auto now_ts{std::chrono::steady_clock::now()};

    bool needs_flush{false};
    if (at_transaction_boundary && transaction_gtid.is_empty()) {
      // a special combination of parameters when at_transaction_boundary is
      // true and transaction_gtid is empty means that we received either ROTATE
      // or STOP event at the very end of a binary log file - in this case we
      // need to flush the event data buffer immediately regardless of whether
      // one of the checkpointing events occurred or not
      needs_flush = true;
    } else {
      // here we perform size-based checkpointing calculations based on
      // calculated "ready_to_flush_position" instead of
      // "get_current_position()" directly to take into account that some event
      // data may remain buffered
      needs_flush = (size_checkpointing_enabled() &&
                     (ready_to_flush_position >=
                      last_checkpoint_position_ + checkpoint_size_bytes_)) ||
                    (interval_checkpointing_enabled() &&
                     (now_ts >= last_checkpoint_timestamp_ +
                                    checkpoint_interval_seconds_));
    }
    if (needs_flush) {
      flush_event_buffer_internal();

      last_checkpoint_position_ = ready_to_flush_position;
      last_checkpoint_timestamp_ = now_ts;
    }
  }
}

void storage::close_binlog() {
  ensure_streaming_mode();

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
}

void storage::flush_event_buffer() {
  ensure_streaming_mode();

  if (has_event_data_to_flush()) {
    flush_event_buffer_internal();
  }
}

[[nodiscard]] std::string
storage::get_binlog_uri(std::string_view binlog_name) const {
  return backend_->get_object_uri(binlog_name);
}

void storage::ensure_streaming_mode() const {
  if (construction_mode_ != storage_construction_mode_type::streaming) {
    util::exception_location().raise<std::logic_error>(
        "operation requires storage to be constructed in streaming mode");
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

void storage::flush_event_buffer_internal() {
  assert(!event_buffer_.empty());
  assert(last_transaction_boundary_position_in_event_buffer_ <=
         std::size(event_buffer_));

  const util::const_byte_span transactions_data{
      std::data(event_buffer_),
      last_transaction_boundary_position_in_event_buffer_};
  // writing <last_transaction_boundary_position_in_event_buffer_> bytes from
  // the beginning of the event buffer
  backend_->write_data_to_stream(transactions_data);
  get_current_binlog_record().size +=
      last_transaction_boundary_position_in_event_buffer_;
  if (is_in_gtid_replication_mode()) {
    auto &optional_added_gtids{get_current_binlog_record().added_gtids};
    if (optional_added_gtids.has_value()) {
      *optional_added_gtids += gtids_in_event_buffer_;
    }
  }
  get_current_binlog_record().timestamps.add_range(ready_to_flush_timestamps_);

  save_binlog_metadata(get_current_binlog_record());

  const auto begin_it{std::begin(event_buffer_)};
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
    if (!check_binlog_name(current_binlog_name)) {
      util::exception_location().raise<std::logic_error>(
          "binlog index contains a reference to a binlog with invalid "
          "name");
    }
    if (std::ranges::find(binlog_records_, current_binlog_name,
                          &binlog_record::name) != std::end(binlog_records_)) {
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
        current_binlog_name, 0ULL, std::move(previous_binlog_gtids),
        std::move(added_binlog_gtids), ctime_timestamp_range{});
  }
}

void storage::validate_binlog_index(
    const storage_object_name_container &object_names) const {
  for (auto const &record : binlog_records_) {
    if (!object_names.contains(record.name)) {
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
    binlog_path /= record.name;
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
}

void storage::validate_metadata(replication_mode_type replication_mode) const {
  if (replication_mode != replication_mode_) {
    util::exception_location().raise<std::logic_error>(
        "replication mode provided to initialize storage differs from the one "
        "stored in metadata");
  }
}

void storage::save_metadata() const {
  storage_metadata metadata{};
  metadata.root().get<"mode">() = replication_mode_;
  const auto content{metadata.str()};
  backend_->put_object(metadata_name, util::as_const_byte_span(content));
}

[[nodiscard]] std::string
storage::generate_binlog_metadata_name(std::string_view binlog_name) {
  std::string binlog_metadata_name{binlog_name};
  binlog_metadata_name += storage::binlog_metadata_extension;
  return binlog_metadata_name;
}

[[nodiscard]] storage::binlog_record
storage::load_binlog_metadata(std::string_view binlog_name) const {
  const auto content{
      backend_->get_object(generate_binlog_metadata_name(binlog_name))};
  binlog_file_metadata metadata{content};

  return binlog_record{.name = std::string(binlog_name),
                       .size = metadata.root().get<"size">(),
                       .previous_gtids =
                           metadata.root().get<"previous_gtids">(),
                       .added_gtids = metadata.root().get<"added_gtids">(),
                       .timestamps = {metadata.root().get<"min_timestamp">(),
                                      metadata.root().get<"max_timestamp">()}};
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
}

void storage::save_binlog_metadata(const binlog_record &record) const {
  binlog_file_metadata metadata{};
  metadata.root().get<"size">() = record.size;
  metadata.root().get<"previous_gtids">() = record.previous_gtids;
  metadata.root().get<"added_gtids">() = record.added_gtids;
  metadata.root().get<"min_timestamp">() =
      ctime_timestamp{record.timestamps.get_min_timestamp()};
  metadata.root().get<"max_timestamp">() =
      ctime_timestamp{record.timestamps.get_max_timestamp()};
  const auto content{metadata.str()};
  backend_->put_object(generate_binlog_metadata_name(record.name),
                       util::as_const_byte_span(content));
}

void storage::load_and_validate_binlog_metadata_set(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const storage_object_name_container &object_names,
    const storage_object_name_container &object_metadata_names) {
  for (auto &record : binlog_records_) {
    const auto binlog_metadata_name{generate_binlog_metadata_name(record.name)};
    if (!object_metadata_names.contains(binlog_metadata_name)) {
      util::exception_location().raise<std::logic_error>(
          "missing metadata for a binlog listed in the binlog index");
    }
    auto loaded_binlog_metadata{load_binlog_metadata(record.name)};
    validate_binlog_metadata(loaded_binlog_metadata);
    // validating that the size stored in the metadata matches the actual size
    if (loaded_binlog_metadata.size != object_names.at(record.name)) {
      util::exception_location().raise<std::logic_error>(
          "size from the binlog metadata does not match the actual binlog "
          "size");
    }
    record = std::move(loaded_binlog_metadata);
  }
  // after this loop position_ and gtids_ should store the values from the last
  // binlog file metadata

  if (std::size(object_metadata_names) != std::size(binlog_records_)) {
    util::exception_location().raise<std::logic_error>(
        "found metadata for a non-existing binlog");
  }
}

} // namespace binsrv
