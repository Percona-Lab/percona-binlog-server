// Copyright (c) 2026 Percona and/or its affiliates.
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

#include "operations/sender_single_file_context.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "binsrv/basic_logger.hpp"
#include "binsrv/basic_storage_backend_fwd.hpp"
#include "binsrv/indexed_event_block.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/replication_mode_type.hpp"
#include "binsrv/storage.hpp"
#include "binsrv/storage_core_fwd.hpp"

#include "binsrv/events/checksum_algorithm_type.hpp"
#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_header_view.hpp"
#include "binsrv/events/composite_binlog_name.hpp"
#include "binsrv/events/event_view.hpp"
#include "binsrv/events/format_description_body_impl.hpp"
#include "binsrv/events/format_description_post_header_impl.hpp"
#include "binsrv/events/generic_body_fwd.hpp"
#include "binsrv/events/generic_post_header_fwd.hpp"
#include "binsrv/events/protocol_traits_fwd.hpp"
#include "binsrv/events/reader_context.hpp"

#include "binsrv/gtids/gtid_set.hpp"

#include "operations/event_generation_helpers.hpp"

#include "util/byte_range.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/conversion_helpers.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace operations {

sender_single_file_context::sender_single_file_context(
    binsrv::basic_logger_ptr logger, binsrv::storage_ptr storage,
    std::size_t block_size, binsrv::replication_mode_type replication_mode,
    binsrv::events::composite_binlog_name binlog_name, std::uint64_t position,
    const binsrv::gtids::gtid_set &gtids, bool checksum_enabled)
    : logger_{std::move(logger)}, storage_{std::move(storage)},
      construction_block_size_{block_size},
      construction_replication_mode_{replication_mode},
      construction_binlog_name_{std::move(binlog_name)},
      construction_position_{position}, construction_gtids_borrowed_{&gtids},
      construction_checksum_enabled_{checksum_enabled} {
  assert(logger_);
  assert(storage_);

  if (construction_block_size_ < min_allowed_block_size) {
    util::exception_location().raise<std::invalid_argument>(
        "In sender_single_file_context constructor block size is too small");
  }
  if (construction_binlog_name_.is_empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "In sender_single_file_context constructor binlog name cannot be "
        "empty");
  }
  if (construction_position_ < binsrv::events::magic_binlog_offset) {
    util::exception_location().raise<std::invalid_argument>(
        "In sender_single_file_context constructor position must be greater "
        "than or equal to 4");
  }
}

sender_single_file_context::~sender_single_file_context() = default;

event_fetching_result_type
sender_single_file_context::get_event(util::const_byte_span &event) {
  switch (fsm_state_) {
  case fsm_state_type::artificial_rotate:
    // 'populate_preamble()' may be called when collector has just created
    // a new binlog file (when its size is 4 bytes, but FORMAT_DESCRIPTION
    // and PREVIOUS_GTIDS_LOG events have not been written yet) - we handle
    // this situation by returning 'waiting' until the preamble events are
    // available

    // 'populate_preamble()' never returns 'event_fetching_result_type::eof'
    if (populate_preamble() == event_fetching_result_type::waiting) {
      event = {};
      return event_fetching_result_type::waiting;
    }
    fsm_state_ = fsm_state_type::format_description;
    event = generated_artificial_rotate_;
    return event_fetching_result_type::success;

  case fsm_state_type::format_description:
    // in position-based replication whether we should send
    // PREVIOUS_GTIDS_LOG event or not depends on whether we started
    // sending events from the beginning of the file,
    // in GTID-based replication mode this event should always be sent
    fsm_state_ =
        (construction_replication_mode_ ==
                     binsrv::replication_mode_type::position &&
                 construction_position_ != binsrv::events::magic_binlog_offset
             ? fsm_state_type::main_loop
             : fsm_state_type::previous_gtids_log);
    event = extracted_format_description_;
    return event_fetching_result_type::success;

  case fsm_state_type::previous_gtids_log:
    fsm_state_ = fsm_state_type::main_loop;
    event = extracted_previous_gtids_log_;
    return event_fetching_result_type::success;

  case fsm_state_type::main_loop: {
    const auto populate_result{
        populate_event_block_until_matching_events_found()};
    if (populate_result != event_fetching_result_type::success) {
      if (populate_result == event_fetching_result_type::eof) {
        fsm_state_ = fsm_state_type::finish;
      }
      logger_->log(binsrv::log_severity::info, "sender : fetched EOF");
      event = {};
      return populate_result;
    }
    // at this point the 'event_block_' must be populated and have
    // available events
    assert(event_block_ &&
           event_index_ < event_block_->get_number_of_matching_events());
    event = event_block_->get_event(event_index_);
    ++event_index_;

    // 'fsm_state_' remains 'main_loop' here
    return event_fetching_result_type::success;
  }
  case fsm_state_type::finish:
    util::exception_location().raise<std::logic_error>(
        "get_event() called after EOF has already been reported");
  default:
    util::exception_location().raise<std::runtime_error>(
        "Unexpected FSM state");
  }
}

void sender_single_file_context::discard_from_carry_buffer(std::size_t size) {
  assert(size <= std::size(carry_buffer_));
  if (size == std::size(carry_buffer_)) {
    carry_buffer_.clear();
    carry_buffer_position_ = 0ULL;
  } else {
    carry_buffer_.erase(std::begin(carry_buffer_),
                        std::next(std::begin(carry_buffer_),
                                  static_cast<std::ptrdiff_t>(size)));
    carry_buffer_position_ += size;
  }
}
void sender_single_file_context::assign_carry_buffer(
    util::const_byte_span data, std::uint64_t carry_buffer_position) {
  carry_buffer_.assign(std::begin(data), std::end(data));
  carry_buffer_position_ = carry_buffer_position;
}

binsrv::storage_fetch_result_type
sender_single_file_context::fetch_with_carry_buffer(
    const util::byte_range &range, util::dynamic_byte_buffer &buffer) {
  assert(range.has_length());
  if (range.get_length() > binsrv::max_storage_backend_memory_object_size) {
    util::exception_location().raise<std::out_of_range>(
        "The requested range is too large for the fetch with carry buffer");
  }

  if (!carry_buffer_.empty() && range.get_offset() == carry_buffer_position_) {
    const auto carried_size{static_cast<std::size_t>(std::min(
        range.get_length(), util::maybe_useless_integral_cast<std::uint64_t>(
                                carry_buffer_.size())))};
    const auto carried_span{
        util::const_byte_span{carry_buffer_}.first(carried_size)};
    util::dynamic_byte_buffer result_buffer{std::begin(carried_span),
                                            std::end(carried_span)};
    auto fetch_result{binsrv::storage_fetch_result_type::success_full};
    if (carried_size < range.get_length()) {
      const util::byte_range missing_range{range.get_offset() + carried_size,
                                           range.get_length() - carried_size};
      util::dynamic_byte_buffer missing_buffer;
      fetch_result = storage_->fetch_event_block(construction_binlog_name_,
                                                 missing_range, missing_buffer);
      result_buffer.insert(std::end(result_buffer), std::begin(missing_buffer),
                           std::end(missing_buffer));
      if (fetch_result != binsrv::storage_fetch_result_type::success_full) {
        fetch_result = binsrv::storage_fetch_result_type::success_truncated;
      }
    }

    discard_from_carry_buffer(carried_size);
    buffer = std::move(result_buffer);
    logger_->log_format(
        binsrv::log_severity::info,
        "sender : reusing {} byte(s) carried from previous fetch",
        carried_size);

    return fetch_result;
  }

  const auto fetch_result{
      storage_->fetch_event_block(construction_binlog_name_, range, buffer)};
  logger_->log_format(binsrv::log_severity::info,
                      "sender : fetched event block of size {}, {}:{}",
                      std::size(buffer), construction_binlog_name_.str(),
                      range.to_string());
  return fetch_result;
}

[[nodiscard]] std::size_t sender_single_file_context::calculate_fde_size(
    std::uint32_t encoded_server_version) {
  return binsrv::events::default_common_header_length +
         binsrv::events::generic_post_header_impl<
             binsrv::events::code_type::format_description>::
             get_size_in_bytes(encoded_server_version) +
         binsrv::events::generic_body_impl<
             binsrv::events::code_type::format_description>::size_in_bytes +
         // FORMAT_DESCRIPTION event always has footer
         binsrv::events::default_footer_length;
}

void sender_single_file_context::process_format_description() {
  // FORMAT_DESCRIPTION events always have checksums

  // server version is not used during the parsing of the FORMAT_DESCRIPTION
  // events, so 0 is OK here
  const binsrv::events::reader_context fake_ctx{
      0U,   /* connection_encoded_server_version */
      true, /* checksum_verification_enabled */
      binsrv::replication_mode_type::position, /* replication_mode */
      "",                                      /* binlog_name */
      0U,                                      /* position */
  };
  const util::byte_span fde_span{extracted_format_description_};
  const binsrv::events::event_updatable_view fde_uv{fake_ctx, fde_span};

  const auto fde_common_header_v{fde_uv.get_common_header_view()};

  if (fde_common_header_v.get_type_code() !=
      binsrv::events::code_type::format_description) {
    util::exception_location().raise<std::logic_error>(
        "The first event in the preamble is not FORMAT_DESCRIPTION");
  }

  const binsrv::events::generic_body_impl<
      binsrv::events::code_type::format_description>
      fde_body{fde_uv.get_body_raw()};

  extracted_checksum_enabled_ =
      (fde_body.get_checksum_algorithm() ==
       binsrv::events::checksum_algorithm_type::crc32);
  extracted_server_id_ = fde_common_header_v.get_server_id_raw();

  binsrv::events::generic_post_header_impl<
      binsrv::events::code_type::format_description>
      fde_post_header{fde_uv.get_post_header_raw()};
  extracted_encoded_server_version_ =
      fde_post_header.get_encoded_server_version();

  // TODO: the rules for the GTID-based replication in MySQL Server are much
  //       more complicated. The 'created' timestamp is cleared only in the
  //       case when no binlog name was provided and MySQL Server went through
  //       the process of auto-positioning. The 'created  timestamp must be
  //       cleared only when the first GTID of the chosen start file is in the
  //       client's set

  // for now, we follow a simplified approach

  // in position-based mode:
  // * if position == 4, leave FDE as is
  // * if position != 4, set 'next_event_pos' in the common header to 0 and
  //   'create_timestamp' in the post header to 0
  // in GTID-based mode:
  // * always set 'create_timestamp' in the post header to 0
  if (construction_replication_mode_ == binsrv::replication_mode_type::gtid ||
      construction_position_ != binsrv::events::magic_binlog_offset) {
    const auto write_proxy{fde_uv.get_write_proxy()};
    if (construction_replication_mode_ ==
        binsrv::replication_mode_type::position) {
      const auto fde_common_header_uv{
          write_proxy.get_common_header_updatable_view()};
      fde_common_header_uv.set_next_event_position_raw(0U);
    }
    auto fde_post_header_span{write_proxy.get_post_header_updatable_raw()};
    fde_post_header.set_create_timestamp_raw(0U);
    fde_post_header.encode_to(fde_post_header_span);
  }
}

void sender_single_file_context::process_previous_gtids() {
  const util::const_byte_span previous_gtids_span{
      extracted_previous_gtids_log_};
  const binsrv::events::common_header_view previous_gtids_common_header_v{
      previous_gtids_span.first(binsrv::events::default_common_header_length)};

  if (previous_gtids_common_header_v.get_type_code() !=
      binsrv::events::code_type::previous_gtids_log) {
    util::exception_location().raise<std::logic_error>(
        "The second event in the preamble is not PREVIOUS_GTIDS_LOG");
  }
}

void sender_single_file_context::generate_artificial_rotate() {
  // for artificial events it is OK to pass magic_binlog_offset as the
  // offset as it will be ignored anyway
  generate_rotate_event_ex(
      generated_artificial_rotate_, construction_checksum_enabled_,
      binsrv::events::magic_binlog_offset, false /* zero timestamp */,
      extracted_server_id_, true /* artificial */, construction_binlog_name_,
      construction_position_);
}

[[nodiscard]] event_fetching_result_type
sender_single_file_context::populate_preamble() {
  assert(fsm_state_ == fsm_state_type::artificial_rotate);

  // we start from reading FORMAT_DESCRIPTION and PREVIOUS_GTIDS_LOG events
  // at the beginning of the binlog file

  // when we read from the beginning of the file ('construction_position_' ==
  // 4), we can simply read the whole 'block_size' at once to cover the preamble
  // events, because other events following them won't be thrown away - they
  // will be taken from the 'carry_buffer_' on next read

  // on the other hand, when we start from a specific position
  // ('construction_position_' > 4), we need to read a data block large enough
  // to cover only the preamble events - we can estimate the size of the
  // FORMAT_DESCRIPTION but we can only guess the size of the
  // PREVIOUS_GTIDS_LOG event

  auto approximate_preamble_size{construction_block_size_};

  const auto max_fde_size{
      calculate_fde_size(binsrv::events::latest_known_protocol_server_version)};

  if (construction_position_ != binsrv::events::magic_binlog_offset) {
    static constexpr auto estimated_previous_gtids_log_size{
        min_allowed_block_size / 2UZ};
    approximate_preamble_size =
        max_fde_size + estimated_previous_gtids_log_size;
  }

  // 'approximate_preamble_size' must be large enough to include full
  // FORMAT_DESCRIPTION event and at least common header of the
  // PREVIOUS_GTIDS_LOG event
  assert(approximate_preamble_size >=
         binsrv::events::default_common_header_length);
  util::byte_range preamble_block_range{binsrv::events::magic_binlog_offset,
                                        approximate_preamble_size};

  util::dynamic_byte_buffer preamble_buffer{};

  const auto fetch_result{storage_->fetch_event_block(
      construction_binlog_name_, preamble_block_range, preamble_buffer)};
  switch (fetch_result) {
  case binsrv::storage_fetch_result_type::success_full:
  case binsrv::storage_fetch_result_type::success_truncated:
    break;
  case binsrv::storage_fetch_result_type::non_active_file_eof:
    util::exception_location().raise<std::runtime_error>(
        "Failed to fetch preamble block from storage");
  case binsrv::storage_fetch_result_type::active_file_eof:
    return event_fetching_result_type::waiting;
  }

  // TODO: in position-based replication in read with offset mode
  //       there is no need to read PREVIOUS_GTIDS_LOG, so this logic could
  //       be optimized.

  // we are interested in the first two events: FORMAT_DESCRIPTION and
  // PREVIOUS_GTIDS_LOG

  // identifying transactions in the preamble does not make sense,
  // therefore 'by_event'

  // using 'by_event' 'indexed_event_block' constructor alias here
  binsrv::indexed_event_block preamble_indexed_block{std::move(preamble_buffer),
                                                     2UZ};
  switch (preamble_indexed_block.get_number_of_matching_events()) {
  case 1UZ: {
    // this is the case where 'approximate_preamble_size' was not enough
    // to include the full PREVIOUS_GTIDS_LOG event
    const auto previous_gtids_size{
        preamble_indexed_block.get_first_entity_size_in_unparsed_tail()};
    if (!previous_gtids_size.has_value()) {
      util::exception_location().raise<std::runtime_error>(
          "Failed to determine size of PREVIOUS_GTIDS_LOG event");
    }
    // approximate_preamble_size was not enough for the PREVIOUS_GTIDS_LOG event
    assign_carry_buffer(preamble_indexed_block.get_full_span(),
                        preamble_block_range.get_offset());
    preamble_block_range = util::byte_range{
        binsrv::events::magic_binlog_offset,
        preamble_indexed_block.get_parsed_size() + *previous_gtids_size};
    // in this additional fetch we expect only 'success_full' - everything
    // else indicates a corruption
    if (fetch_with_carry_buffer(preamble_block_range, preamble_buffer) !=
        binsrv::storage_fetch_result_type::success_full) {
      util::exception_location().raise<std::runtime_error>(
          "Failed to fetch the remainder of PREVIOUS_GTIDS_LOG event");
    }
    // using 'by_event' 'indexed_event_block' constructor alias here
    preamble_indexed_block =
        binsrv::indexed_event_block{std::move(preamble_buffer), 2UZ};
    if (preamble_indexed_block.get_number_of_matching_events() != 2UZ) {
      util::exception_location().raise<std::runtime_error>(
          "Encountered corrupted preamble block after fetching the remainder "
          "of PREVIOUS_GTIDS_LOG event");
    }
  } break;
  case 2UZ:
    // this is the case we want as here preamble_indexed_block contains both
    // FORMAT_DESCRIPTION and PREVIOUS_GTIDS_LOG events
    break;
  default:
    util::exception_location().raise<std::runtime_error>(
        "Encountered corrupted preamble block");
  }
  assert(preamble_indexed_block.get_number_of_matching_events() == 2UZ);

  const auto format_description_span{preamble_indexed_block.get_event(0UZ)};
  extracted_format_description_.assign(std::cbegin(format_description_span),
                                       std::cend(format_description_span));
  // this function may transform the format description event to an
  // artificial one in certain scenarios
  process_format_description();

  const auto previous_gtids_log_span{preamble_indexed_block.get_event(1UZ)};
  extracted_previous_gtids_log_.assign(std::cbegin(previous_gtids_log_span),
                                       std::cend(previous_gtids_log_span));
  process_previous_gtids();

  // after these assignments we may still have useful data in the tail of the
  // 'preamble_indexed_block' - so, it should be transferred to the carry
  // buffer
  if (preamble_indexed_block.get_unparsed_tail_size() > 0UZ) {
    assign_carry_buffer(preamble_indexed_block.get_unparsed_tail_span(),
                        preamble_indexed_block.get_parsed_size() +
                            binsrv::events::magic_binlog_offset);
  }

  // in case when requested position was either the beginning of the binlog
  // file (magic_binlog_offset 4) we will continue main event loop from the
  // very first event after the preamble (should be one of the GTID events)

  // TODO: investigate how MySQL Server behaves in the situation when
  //       the specified position points to the PREVIOUS_GTIDS_LOG event
  if (construction_position_ == binsrv::events::magic_binlog_offset) {
    current_position_ = binsrv::events::magic_binlog_offset +
                        preamble_indexed_block.get_parsed_size();
  } else {
    current_position_ = construction_position_;
  }

  generate_artificial_rotate();

  return event_fetching_result_type::success;
}

[[nodiscard]] event_fetching_result_type
sender_single_file_context::populate_event_block() {
  assert(fsm_state_ == fsm_state_type::main_loop);

  // early return if we have a non-empty event block and have not consumed
  // all events in it

  if (event_block_ &&
      event_index_ < event_block_->get_number_of_matching_events()) {
    return event_fetching_result_type::success;
  }

  // preserving the unparsed tail of the current event block before resetting it
  if (event_block_) {
    current_position_ += event_block_->get_parsed_size();
    assign_carry_buffer(event_block_->get_unparsed_tail_span(),
                        current_position_);
  }
  // early reset to free memory from the previous event block
  event_block_.reset();

  util::byte_range fetch_range{current_position_, construction_block_size_};
  util::dynamic_byte_buffer fetch_buffer;
  auto fetch_result{fetch_with_carry_buffer(fetch_range, fetch_buffer)};
  if (fetch_result == binsrv::storage_fetch_result_type::non_active_file_eof) {
    return event_fetching_result_type::eof;
  }
  if (fetch_result == binsrv::storage_fetch_result_type::active_file_eof) {
    return event_fetching_result_type::waiting;
  }

  const auto indexed_event_block_mode{
      construction_replication_mode_ == binsrv::replication_mode_type::position
          ? binsrv::indexed_event_block_mode_type::by_event
          : binsrv::indexed_event_block_mode_type::by_transaction};
  event_block_ = std::make_unique<binsrv::indexed_event_block>(
      indexed_event_block_mode, std::move(fetch_buffer),
      *construction_gtids_borrowed_, extracted_encoded_server_version_,
      extracted_checksum_enabled_);

  if (event_block_->get_parsed_size() == 0UZ) {
    assign_carry_buffer(event_block_->get_unparsed_tail_span(),
                        current_position_);

    const auto optional_first_event_size{
        event_block_->get_first_entity_size_in_unparsed_tail()};
    if (!optional_first_event_size.has_value()) {
      util::exception_location().raise<std::runtime_error>(
          "Cannot determine the size of the first event in the truncated "
          "block");
    }
    const auto first_event_size{*optional_first_event_size};
    fetch_range = util::byte_range{current_position_, first_event_size};
    logger_->log_format(
        binsrv::log_severity::info,
        "sender : block too small for first event (needs {} bytes)",
        first_event_size);

    fetch_result = fetch_with_carry_buffer(fetch_range, fetch_buffer);
    // here partial fetch is considered an error as collector never writes
    // partial events
    if (fetch_result != binsrv::storage_fetch_result_type::success_full) {
      util::exception_location().raise<std::runtime_error>(
          "Failed to fetch block with at least one event");
    }
    event_block_ = std::make_unique<binsrv::indexed_event_block>(
        indexed_event_block_mode, std::move(fetch_buffer),
        *construction_gtids_borrowed_, extracted_encoded_server_version_,
        extracted_checksum_enabled_);
  }
  if (event_block_->get_parsed_size() == 0UZ) {
    util::exception_location().raise<std::runtime_error>(
        "Failed to fetch block with at least one event after retry");
  }
  logger_->log_format(binsrv::log_severity::info,
                      "sender : parsed event block with {} matching events, "
                      "actual size {} byte(s)",
                      event_block_->get_number_of_matching_events(),
                      event_block_->get_parsed_size());

  event_index_ = 0UZ;
  return event_fetching_result_type::success;
}

[[nodiscard]] event_fetching_result_type
sender_single_file_context::populate_event_block_until_matching_events_found() {
  // in GTID-based mode, when we do transaction filtering by the specified
  // GTID set there is a chance that every transaction in the fetched block
  // will be skipped - in this case we need to continue fetching and populating
  // event blocks until we find a one with at least one not filtered event
  event_fetching_result_type populate_result{};
  while ((populate_result = populate_event_block()) ==
             event_fetching_result_type::success &&
         event_block_->get_number_of_matching_events() == 0UZ) {
  }
  return populate_result;
}

[[nodiscard]] bool
sender_single_file_context::generate_heartbeat(util::const_byte_span &event) {
  // A heartbeat reports the file name, server_id and read position reached so
  // far, all of which only become known once the preamble (artificial ROTATE,
  // FORMAT_DESCRIPTION and - at the start of a file - PREVIOUS_GTIDS_LOG) has
  // been consumed and we have entered the main loop. A MySQL source behaves the
  // same way: it only enters its heartbeat wait loop once positioned on the
  // active file past the format description event
  // (sql/rpl_binlog_sender.cc Binlog_sender::wait_with_heartbeat). Before that
  // the fields above are still unset, so we report "not ready" and let the
  // caller keep polling for the preamble to appear rather than emit a heartbeat
  // with a zero position and server_id.
  if (fsm_state_ != fsm_state_type::main_loop) {
    event = {};
    return false;
  }
  // 'current_position_' is the offset we have read up to in this file; while
  // the dump sits idle at EOF this is the end of the file - exactly the
  // position upstream reports in its heartbeats. 'extracted_checksum_enabled_'
  // and 'extracted_server_id_' match the FORMAT_DESCRIPTION event the replica
  // has already seen.
  generate_heartbeat_log_v2_event_ex(
      generated_heartbeat_, extracted_checksum_enabled_, extracted_server_id_,
      construction_binlog_name_, current_position_);
  event = generated_heartbeat_;
  return true;
}

} // namespace operations
