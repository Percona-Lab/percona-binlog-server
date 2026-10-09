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

#include "operations/sender_context.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string_view>
#include <utility>

#include "binsrv/basic_logger.hpp"
#include "binsrv/indexed_event_block.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/replication_mode_type.hpp"
#include "binsrv/storage.hpp"

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

#include "operations/event_generation_helpers.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/common_optional_types.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"

namespace operations {

sender_context::sender_context(binsrv::basic_logger_ptr logger,
                               binsrv::storage_ptr storage,
                               std::size_t block_size,
                               std::string_view binlog_name,
                               std::uint64_t position,
                               bool session_source_binlog_checksum)
    : logger_{std::move(logger)}, storage_{std::move(storage)},
      block_size_{block_size},
      current_binlog_checksum_{session_source_binlog_checksum},
      fsm_state_{position == binsrv::events::magic_binlog_offset
                     ? fsm_state_type::start_from_beginning
                     : fsm_state_type::start_from_offset},
      position_for_artificial_rotate_{position},
      // an empty binlog name in the COM_BINLOG_DUMP request means "the
      // oldest binlog file available" - in this case we keep 'binlog_name_'
      // empty and let 'storage::fetch_event_block()' resolve it
      binlog_name_{
          binlog_name.empty()
              ? binsrv::events::composite_binlog_name{}
              : binsrv::events::composite_binlog_name::parse(binlog_name)},
      range_{position, block_size_} {
  assert(block_size_ > 0UZ);
  assert(storage_);
  assert(logger_);
}

sender_context::~sender_context() = default;

[[nodiscard]] bool sender_context::get_event(util::const_byte_span &event) {
  // Additional (artificial) events that need to be generated.

  // When client requests replication from ["binlog.<n>":4] or from ["":4]
  // (from the magic offset, meaning the beginning of the binlog file),
  // replication source must send the following sequence of events.
  // 1. A generated artificial ROTATE with the 'position' field set to 4 and
  //    the 'binlog' field set to "binlog.<n>" (in case of an empty binlog
  //    name specified in the request, the 'binlog' field must be set to
  //    the oldest binlog file available on the server). Whether this message
  //    should include checksum or not must be determined from the
  //    '@source_binlog_checksum' / '@master_binlog_checksum' session
  //    variable set in MySQL connection before switching to replication
  //    mode. 'timestamp' and 'next_event_position' fields in the common
  //    header of this event must be set to 0. The 'flags' field in the
  //    common header must be set to 'artificial'.
  // 2. The very first event in the "binlog.<n>" (or resolved oldest binlog
  //    file). It must be the FORMAT_DESCRIPTION event. This event must
  //    always include checksum (regardless of client or server settings).
  // 3. Subsequent events in that "binlog.<n>" binlog file.
  // 4. The last event in the "binlog.<n>" (can be one of the following).
  //    a. In the most common case it must be a real ROTATE event with the
  //       'binlog' field set to "binlog.<n+1>", a non-zero 'timestamp',
  //       a non-zero 'next_event_position', and 'flags' field not
  //       containing the 'artificial' bit.
  //    b. After MySQL Server shutdown, the last event in a binlog file
  //       might be a STOP event.
  //    c. In rare cases, after improper shutdown, the binlog may end simply
  //       with the last event in a complete transaction (usually XID event).
  // 5. A generated artificial ROTATE with 'binlog' field set to
  //    "binlog.<n+1>" and position set to 4. Whether this event should
  //    include checksum or not depends on the value of the
  //    'checksum_algorithm' field in the last seen FORMAT_DESCRIPTION event,
  //    the event from (2) in this sequence.
  // 6. Similar to (2), but for "binlog.<n+1>".
  // 7. Similar to (3), but for "binlog.<n+1>".
  // 8. Similar to (4), but for "binlog.<n+1>".
  // ...
  // N. Repeat steps (5)-(8) for subsequent binlog files.
  // If there are no more events in the last binlog file, EOF is returned
  // (or, in blocking mode, the caller polls until new events appear).

  // In case when client requests replication from a position that is not
  // equal to "magic offset" 4, there are a few changes to the rules
  // described above.
  // 1. Almost identical to (1), but the 'position' field in the artificial
  //    ROTATE event must be set to the requested position. Empty binlog name
  //    is not supported in this case.
  // 2. Instead of (2) (a FORMAT_DESCRIPTION event taken from the binlog
  //    file as is), we must send an artificial FORMAT_DESCRIPTION event.
  //    It is constructed from the real FORMAT_DESCRIPTION event located at
  //    the beginning of "binlog.<n>" by setting 'next_event_position' field
  //    in the common header and 'create_timestamp' field in the post header
  //    to 0 (all the other fields, including 'timestamp' and 'flags' in the
  //    common header, are kept as is). The checksum is recalculated.
  // 3. Events from (3) are sent starting from the requested position.
  // These two artificial events, (1) and (2), must be sent even when the
  // requested position is equal to the size of "binlog.<n>" (meaning that
  // there are no more events to send from this file). In this case, if
  // "binlog.<n+1>" exists, the sequence continues immediately from step (5)
  // of the rules above. Otherwise, EOF is returned.

  // In order to implement this logic, we use the following FSM
  //                           |                |
  //                           v                |
  //            (start_from_offset)             |
  //                           |                |
  //                           v                |
  //      (generate_artificial_fde)             |
  //                           |                |
  //                           |                v
  //                           |     (start_from_beginning) <----+
  //                           |                |                |
  //                           v                v                |
  //                +----> (fetch_event_from_storage)            |
  //                |       |                      |             |
  //                +-------+                      +-------------+
  //   [ binlog file not changed ]         [  binlog file changed ]
  //
  // 'start_from_offset' and 'generate_artificial_fde' states do not require
  // any event block to be fetched from the storage ('start_from_offset'
  // reads the FORMAT_DESCRIPTION event from the beginning of "binlog.<n>"
  // directly). 'start_from_beginning' state, on the other hand, requires
  // the first event block to be fetched first, as this is how an empty
  // binlog name gets resolved and how switching to the next binlog file
  // gets detected.

  // the 'start_from_offset' and 'generate_artificial_fde' states must be
  // handled before any event block is fetched: the artificial ROTATE /
  // FORMAT_DESCRIPTION pair for the requested binlog file must be sent even
  // when the requested position is equal to the size of that file (in which
  // case the very first fetch would either return EOF or would switch to the
  // next binlog file)

  // a branch with early return for the 'start_from_offset' state
  if (fsm_state_ == fsm_state_type::start_from_offset) {
    return handle_start_states(event);
  }

  // a branch with early return for the 'generate_artificial_fde' state
  if (fsm_state_ == fsm_state_type::generate_artificial_fde) {
    return handle_generate_artificial_fde_state(event);
  }

  const auto populate_result{populate_event_block(event)};
  if (populate_result.has_value()) {
    return *populate_result;
  }

  // a branch with early return for the 'start_from_beginning' state
  if (fsm_state_ == fsm_state_type::start_from_beginning) {
    return handle_start_states(event);
  }

  // The main branch for the 'fetch_event_from_storage' state
  return handle_fetch_event_from_storage_state(event);
}

[[nodiscard]] std::size_t
sender_context::calculate_fde_size(std::uint32_t encoded_server_version) {
  return binsrv::events::default_common_header_length +
         binsrv::events::generic_post_header_impl<
             binsrv::events::code_type::format_description>::
             get_size_in_bytes(encoded_server_version) +
         binsrv::events::generic_body_impl<
             binsrv::events::code_type::format_description>::size_in_bytes +
         // FORMAT_DESCRIPTION event always has footer
         binsrv::events::default_footer_length;
}

[[nodiscard]] bool sender_context::fetch_fde_from_storage() {
  // 'range_' has already been advanced past the fetched event block at this
  // point, so the only meaningful invariant here is the FSM state
  assert(fsm_state_ == fsm_state_type::start_from_offset);
  binsrv::events::composite_binlog_name fde_binlog_name{binlog_name_};
  util::dynamic_byte_buffer fde_buffer{};
  // starting from MySQL Server 8.3 FORMAT_DESCRIPTION event is one byte
  // longer because of the new GTID_TAGGED_LOG_EVENT

  // so here we assume that the max buffer size should be large enough
  // to hold the entire FORMAT_DESCRIPTION event from the most recent
  // known MySQL Server version (>=8.3)
  const auto max_fde_size{
      calculate_fde_size(binsrv::events::latest_known_protocol_server_version)};
  util::byte_range fde_range{binsrv::events::magic_binlog_offset, max_fde_size};

  if (!storage_->fetch_event_block(fde_binlog_name, fde_range, fde_buffer)) {
    return false;
  }
  // the binlog file may be shorter than requested (truncated / corrupt), so
  // we need to make sure that at least the common header fits
  if (std::size(fde_buffer) < binsrv::events::default_common_header_length) {
    return false;
  }
  const binsrv::events::common_header_view fde_common_header_v{
      util::const_byte_span{fde_buffer}.subspan(
          0, binsrv::events::default_common_header_length)};

  if (fde_common_header_v.get_type_code() !=
      binsrv::events::code_type::format_description) {
    return false;
  }

  const auto min_fde_size{calculate_fde_size(
      binsrv::events::earliest_supported_protocol_server_version)};
  const auto real_fde_size{fde_common_header_v.get_event_size_raw()};
  if (real_fde_size < min_fde_size || real_fde_size > max_fde_size) {
    return false;
  }
  // the event size advertised in the common header must not exceed the
  // number of bytes actually fetched
  if (real_fde_size > std::size(fde_buffer)) {
    return false;
  }
  fde_.assign(std::cbegin(fde_buffer), std::cbegin(fde_buffer) + real_fde_size);
  return true;
}

[[nodiscard]] bool sender_context::extract_fde_from_event_block() {
  assert(event_block_);
  assert(!event_block_->is_empty());
  const auto fde_span{event_block_->get_event(0U)};

  const binsrv::events::common_header_view fde_common_header_v{
      util::const_byte_span{fde_span}.subspan(
          0, binsrv::events::default_common_header_length)};
  if (fde_common_header_v.get_type_code() !=
      binsrv::events::code_type::format_description) {
    return false;
  }
  fde_.assign(std::cbegin(fde_span), std::cend(fde_span));
  return true;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void sender_context::extract_fields_from_fde(std::uint32_t &server_id,
                                             bool &checksum_enabled) const {
  // creating a minimally-initialized reader context with only one
  // meaningful field 'checksum_verification_enabled' set to true -
  // creating a view on an FDE is a special case and will not read
  // any other field from the context
  const binsrv::events::reader_context fake_ctx{
      0U,   /* connection_encoded_server_version */
      true, /* checksum_verification_enabled */
      binsrv::replication_mode_type::position, /* replication_mode */
      "",                                      /* binlog_name */
      0U,                                      /* position */
  };
  const util::const_byte_span fde_span{fde_};
  const binsrv::events::event_view fde_v{fake_ctx, fde_span};

  const auto fde_common_header_v{fde_v.get_common_header_view()};

  assert(fde_common_header_v.get_type_code() ==
         binsrv::events::code_type::format_description);

  const binsrv::events::generic_body_impl<
      binsrv::events::code_type::format_description>
      fde_body{fde_v.get_body_raw()};

  server_id = fde_common_header_v.get_server_id_raw();
  checksum_enabled = (fde_body.get_checksum_algorithm() ==
                      binsrv::events::checksum_algorithm_type::crc32);
}

void sender_context::transform_fde_to_artificial() {
  // artificial FDE must have 'next_event_pos' field in the common header
  // set to 0U and 'create_timestamp' in the post header set to 0U
  const binsrv::events::reader_context fake_ctx{
      0U,   /* connection_encoded_server_version */
      true, /* checksum_verification_enabled */
      binsrv::replication_mode_type::position, /* replication_mode */
      "",                                      /* binlog_name */
      0U,                                      /* position */
  };
  const util::byte_span fde_span{fde_};
  const binsrv::events::event_updatable_view fde_uv{fake_ctx, fde_span};
  {
    const auto write_proxy{fde_uv.get_write_proxy()};
    const auto fde_common_header_uv{
        write_proxy.get_common_header_updatable_view()};
    fde_common_header_uv.set_next_event_position_raw(0U);
    auto fde_post_header_span{write_proxy.get_post_header_updatable_raw()};
    binsrv::events::generic_post_header_impl<
        binsrv::events::code_type::format_description>
        fde_post_header{fde_post_header_span};
    fde_post_header.set_create_timestamp_raw(0U);
    fde_post_header.encode_to(fde_post_header_span);
  }
}

[[nodiscard]] util::optional_bool
sender_context::populate_event_block(util::const_byte_span &event) {
  if (event_block_ && event_index_ != event_block_->get_number_of_events()) {
    return {};
  }

  // if this is the very first call when 'event_block_' is not yet set or we
  // have consumed all events in the current block

  // Snapshot the finishing block's unparsed tail into 'carry_buffer_' before
  // destroying the block. We prepend it to the next fetch instead of
  // re-reading those bytes from storage: with the default 1 MiB block size
  // a block whose boundary falls mid-event would otherwise cost a redundant
  // filesystem read, or an S3 GET plus network round-trip, for bytes that
  // are already in RAM.
  if (event_block_) {
    const auto tail{event_block_->get_unparsed_tail()};
    if (!std::empty(tail)) {
      carry_buffer_.assign(std::begin(tail), std::end(tail));
    }
  }
  // early reset to free memory from the previous event block
  event_block_.reset();

  // Seed the combined buffer with whatever we carried from the previous
  // iteration (empty on the very first call).
  util::dynamic_byte_buffer buffer{std::move(carry_buffer_)};
  carry_buffer_.clear();
  if (!std::empty(buffer)) {
    logger_->log_format(
        binsrv::log_severity::info,
        "sender : reusing {} byte(s) carried from previous fetch",
        std::size(buffer));
  }

  // Fetch loop: top up until the buffer holds the full first event, but not
  // less than 'block_size_' (1 MiB by default). In the steady state the
  // carry already contains the first event's common header, so the exact
  // fetch length is known up front and this loop runs exactly once. On a
  // cold start, or when the carry is shorter than a header, the first
  // iteration brings in 'block_size_' bytes to reveal the header and the
  // second iteration (if any) tops up to exactly 'first_event_size'.
  while (true) {
    std::size_t desired_size{block_size_};
    if (std::size(buffer) >=
        binsrv::events::common_header_view_base::size_in_bytes) {
      const binsrv::events::common_header_view header{
          util::const_byte_span{buffer}.subspan(
              0UZ, binsrv::events::common_header_view_base::size_in_bytes)};
      const auto first_event_size{
          static_cast<std::size_t>(header.get_event_size_raw())};
      if (first_event_size > binsrv::events::max_event_size_bytes) {
        // Nonsensically large event size: corrupt or malicious header.
        return false;
      }
      if (std::size(buffer) >= first_event_size) {
        // Defensive: 'indexed_event_block' leaves at most one partial event
        // in the carry, so entering this branch on the first iteration
        // should not happen. On later iterations an exact-sized top-up
        // lands here naturally.
        break;
      }
      if (first_event_size > block_size_) {
        logger_->log_format(
            binsrv::log_severity::info,
            "sender : block too small for first event (needs {} bytes), "
            "topping up to exact size at {}:{}",
            first_event_size, binlog_name_.str(), range_.to_string());
        desired_size = first_event_size;
      }
    }

    const std::size_t fetch_length{desired_size - std::size(buffer)};
    const binsrv::events::composite_binlog_name saved_binlog_name{binlog_name_};
    util::dynamic_byte_buffer fetched_bytes{};
    range_ = util::byte_range{range_.get_offset(), fetch_length};
    // on success both 'binlog_name_' and 'range_' will be updated
    if (!storage_->fetch_event_block(binlog_name_, range_, fetched_bytes)) {
      return false;
    }
    if (std::empty(fetched_bytes)) {
      logger_->log(binsrv::log_severity::info, "sender : fetched EOF");
      // leaving 'binlog_name_' as is so that on next fetch after this EOF
      // we could make another attempt to check if new events were added.
      //
      // 'range_', on the other hand, was set to empty inside
      // 'fetch_event_block()' - here we restore its length for the next
      // fetch attempt. Hand the carry back for the next call to retry the
      // fetch with it still in hand.
      if (!std::empty(buffer)) {
        carry_buffer_ = std::move(buffer);
      }
      range_ = util::byte_range{range_.get_offset(), block_size_};
      event_block_.reset();
      event_index_ = 0UZ;

      // setting the event span to an empty object to indicate EOF
      event = util::const_byte_span{};
      return true; // EOF
    }
    if (saved_binlog_name.is_empty()) {
      logger_->log_format(binsrv::log_severity::info,
                          "sender : empty binlog name resolved to {}",
                          binlog_name_.str());
    } else if (binlog_name_ != saved_binlog_name) {
      // 'fetch_event_block()' switches to the next binlog file only when
      // the current offset is at EOF of the current one, and a single call
      // never spans two files. MySQL binlog events never cross file
      // boundaries, so a non-empty carry at a file switch means the
      // previous file ended on a partial event (truncated / corrupt).
      // Silently dropping those bytes would mask data loss on the
      // replication path, so error out.
      if (!std::empty(buffer)) {
        logger_->log_format(binsrv::log_severity::error,
                            "sender : {} byte(s) of partial event at EOF of "
                            "binlog file {}, boundary to {} - corrupt binlog",
                            std::size(buffer), saved_binlog_name.str(),
                            binlog_name_.str());
        return false;
      }
      logger_->log_format(binsrv::log_severity::info,
                          "sender : switched to a new binlog file {} -> {}",
                          saved_binlog_name.str(), binlog_name_.str());
      fsm_state_ = fsm_state_type::start_from_beginning;
    }
    logger_->log_format(binsrv::log_severity::info,
                        "sender : fetched event block of size {}, {}:{}",
                        std::size(fetched_bytes), binlog_name_.str(),
                        range_.to_string());
    const auto fetched_length{std::size(fetched_bytes)};
    buffer.insert(std::end(buffer), std::begin(fetched_bytes),
                  std::end(fetched_bytes));
    // Advance the storage offset past every byte we fetched (not just past
    // the complete events). Any trailing partial-event bytes stay in RAM
    // inside the resulting 'indexed_event_block' and migrate into
    // 'carry_buffer_' on the next entry, so storage never has to serve
    // them twice.
    range_ =
        util::byte_range{range_.get_offset() + fetched_length, block_size_};
  }

  event_block_ =
      std::make_unique<binsrv::indexed_event_block>(std::move(buffer));
  if (event_block_->is_empty()) {
    // Defensive: the loop only exits when 'buffer' holds a complete first
    // event, so 'indexed_event_block' should always find at least one.
    return false;
  }
  event_index_ = 0UZ;
  logger_->log_format(
      binsrv::log_severity::info,
      "sender : parsed event block with {} events, actual size {} byte(s)",
      event_block_->get_number_of_events(), event_block_->get_actual_size());
  return {};
}

[[nodiscard]] bool
sender_context::handle_start_states(util::const_byte_span &event) {
  assert(fsm_state_ == fsm_state_type::start_from_beginning ||
         fsm_state_ == fsm_state_type::start_from_offset);
  // in case when we start from the 'start_from_offset' state, in addition to
  // fetching real events requested in the range, we also need to receive
  // FORMAT_DESCRIPTION event located at the beginning of this binlog file
  if (fsm_state_ == fsm_state_type::start_from_offset) {
    // as the 'start_from_offset' state is handled before any event block is
    // fetched, an empty binlog name has not been resolved yet - starting from
    // a non-magic offset of an unspecified binlog file is not supported
    if (binlog_name_.is_empty()) {
      logger_->log(binsrv::log_severity::error,
                   "sender : cannot start from a position other than 4 without "
                   "specifying binlog name");
      return false;
    }
    if (!fetch_fde_from_storage()) {
      return false;
    }
  } else {
    if (!extract_fde_from_event_block()) {
      return false;
    }
  }

  std::uint32_t fde_server_id{};
  bool fde_checksum_enabled{};
  extract_fields_from_fde(fde_server_id, fde_checksum_enabled);

  // for artificial events it is OK to pass magic_binlog_offset as the
  // offset as it will be ignored anyway
  generate_rotate_event_ex(artificial_rotate_, current_binlog_checksum_,
                           binsrv::events::magic_binlog_offset,
                           false /* zero timestamp */, fde_server_id,
                           true /* artificial */, binlog_name_,
                           position_for_artificial_rotate_);
  position_for_artificial_rotate_ = binsrv::events::magic_binlog_offset;

  // for the artificial ROTATE events generated in future the decision
  // whether to include footer with a checksum will be based on the
  // 'checksum_algorithm' field from the most recent FDE
  current_binlog_checksum_ = fde_checksum_enabled;

  fsm_state_ = fsm_state_ == fsm_state_type::start_from_beginning
                   ? fsm_state_type::fetch_event_from_storage
                   : fsm_state_type::generate_artificial_fde;

  event = artificial_rotate_;
  return true;
}

[[nodiscard]] bool sender_context::handle_generate_artificial_fde_state(
    util::const_byte_span &event) {
  assert(fsm_state_ == fsm_state_type::generate_artificial_fde);

  // at this point 'fde_' must have already been filled with the first
  // FDE in the current binlog file
  transform_fde_to_artificial();
  fsm_state_ = fsm_state_type::fetch_event_from_storage;

  event = fde_;
  return true;
}

[[nodiscard]] bool sender_context::handle_fetch_event_from_storage_state(
    util::const_byte_span &event) {
  assert(fsm_state_ == fsm_state_type::fetch_event_from_storage);

  event = event_block_->get_event(event_index_);
  ++event_index_;
  return true;
}

} // namespace operations
