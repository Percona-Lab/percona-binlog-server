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

namespace {

// FDE view construction only consults the reader context for fields that
// the FDE paths special-case: the FDE always has a 4-byte footer regardless
// of the body's checksum_algorithm, and the FDE's post-header length is
// derived from the body-embedded server version rather than the context's
// post-header-lengths table. So this placeholder context is sufficient for
// both reading an FDE (event_view) and transforming it in place
// (event_updatable_view + write_proxy).
[[nodiscard]] binsrv::events::reader_context make_fde_only_context() {
  return binsrv::events::reader_context{
      0U, true, binsrv::replication_mode_type::position, "", 0U,
  };
}

} // namespace

sender_context::sender_context(binsrv::basic_logger_ptr logger,
                               binsrv::storage_ptr storage,
                               std::size_t block_size,
                               std::string_view binlog_name,
                               std::uint64_t position,
                               bool session_source_binlog_checksum)
    : logger_{std::move(logger)}, storage_{std::move(storage)},
      block_size_{block_size},
      current_binlog_checksum_{session_source_binlog_checksum},
      position_for_next_artificial_rotate_{position},
      // An empty binlog_name in the COM_BINLOG_DUMP request means "the
      // oldest binlog file available" - leave binlog_name_ empty and let
      // storage::fetch_event_block() resolve it on the opener fetch.
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
  // Protocol opener (synthesized on the very first call):
  //
  //   when requested position == magic_binlog_offset (4):
  //     [artificial ROTATE] -> [real on-disk FDE] -> real events...
  //
  //   when requested position != magic_binlog_offset:
  //     [artificial ROTATE] -> [transformed FDE] -> real events from
  //                                                 the requested position...
  //
  // On a subsequent storage-driven file switch (handled inside
  // populate_event_block) one more artificial ROTATE is slotted in before
  // the new file's real FDE flows out of event_block_.
  //
  // See upstream MySQL `sql/rpl_binlog_sender.cc` for the reference master
  // behavior this emulates.

  if (!opener_initialized_) {
    if (!initialize_opener()) {
      return false;
    }
    opener_initialized_ = true;
  }

  if (artificial_rotate_pending_) {
    event = artificial_rotate_;
    artificial_rotate_pending_ = false;
    return true;
  }

  if (transformed_fde_pending_) {
    event = transformed_fde_;
    transformed_fde_pending_ = false;
    return true;
  }

  if (!event_block_ || event_index_ == event_block_->get_number_of_events()) {
    const auto fetch_result{populate_event_block(event)};
    if (fetch_result.has_value()) {
      // EOF (*fetch_result == true, event already cleared) or error (false).
      return *fetch_result;
    }
    // A fresh block loaded. If storage transitioned to a new binlog file,
    // populate_event_block will have slotted in a file-switch artificial
    // ROTATE; drain it here before touching the real events in the block.
    if (artificial_rotate_pending_) {
      event = artificial_rotate_;
      artificial_rotate_pending_ = false;
      return true;
    }
  }

  event = event_block_->get_event(event_index_);
  ++event_index_;
  return true;
}

[[nodiscard]] bool sender_context::initialize_opener() {
  // Regardless of the requested resume position, the opener needs the real
  // on-disk FDE at offset 4 of the resolved file to pull server_id and
  // checksum_algorithm out of. Fetch a normal block there; event 0 is the
  // FDE.
  util::byte_range opener_range{binsrv::events::magic_binlog_offset,
                                block_size_};
  util::dynamic_byte_buffer buffer{};
  const binsrv::events::composite_binlog_name saved_binlog_name{binlog_name_};
  if (!storage_->fetch_event_block(binlog_name_, opener_range, buffer)) {
    return false;
  }
  if (saved_binlog_name.is_empty() && !binlog_name_.is_empty()) {
    logger_->log_format(binsrv::log_severity::info,
                        "sender : empty binlog name resolved to {}",
                        binlog_name_.str());
  }

  if (buffer.empty()) {
    // Nothing in storage yet. Leave the opener in its "no pending events"
    // shape; the main get_event loop will fall through to populate_event_block
    // which will cleanly report EOF to the caller.
    range_ = util::byte_range{range_.get_offset(), block_size_};
    return true;
  }

  // Validate the first event's common header before handing bytes to the
  // indexed_event_block parser (which assumes a well-formed stream).
  if (std::size(buffer) <
      binsrv::events::common_header_view_base::size_in_bytes) {
    return false;
  }
  const binsrv::events::common_header_view opener_first_header{
      util::const_byte_span{buffer}.subspan(
          0UZ, binsrv::events::common_header_view_base::size_in_bytes)};
  if (opener_first_header.get_type_code() !=
      binsrv::events::code_type::format_description) {
    return false;
  }
  const auto fde_size{
      static_cast<std::size_t>(opener_first_header.get_event_size_raw())};
  if (fde_size > binsrv::events::max_event_size_bytes) {
    return false;
  }
  event_block_ =
      std::make_unique<binsrv::indexed_event_block>(std::move(buffer));
  if (event_block_->is_empty()) {
    return false;
  }
  event_index_ = 0UZ;

  const auto fde_bytes{event_block_->get_event(0)};
  std::uint32_t fde_server_id{};
  bool fde_checksum_enabled{};
  extract_fields_from_fde(fde_bytes, fde_server_id, fde_checksum_enabled);

  // The first artificial ROTATE uses whatever the client negotiated on the
  // SET @source_binlog_checksum line (passed in as
  // session_source_binlog_checksum and stashed as current_binlog_checksum_
  // in the ctor). After this point current_binlog_checksum_ tracks the most
  // recently seen FDE's checksum, so every subsequent artificial ROTATE
  // (file switches) honors the stream's actual checksum setting.
  //
  // The offset argument to generate_rotate_event_ex is irrelevant for
  // artificial events (next_event_position is forced to 0), so passing
  // magic_binlog_offset here is a don't-care placeholder.
  generate_rotate_event_ex(artificial_rotate_, current_binlog_checksum_,
                           binsrv::events::magic_binlog_offset,
                           false /* zero timestamp */, fde_server_id,
                           true /* artificial */, binlog_name_,
                           position_for_next_artificial_rotate_);
  artificial_rotate_pending_ = true;

  const bool mid_file_resume{position_for_next_artificial_rotate_ !=
                             binsrv::events::magic_binlog_offset};
  if (mid_file_resume) {
    // Resuming from the middle of a file. Transform the real FDE into an
    // artificial one (next_event_position=0, create_timestamp=0, CRC
    // recalculated) and send it right after the ROTATE. Discard
    // event_block_ and re-aim range_ at the requested position so the next
    // populate_event_block fetches the actual events the client asked for.
    transform_fde_from(fde_bytes);
    transformed_fde_pending_ = true;
    event_block_.reset();
    event_index_ = 0UZ;
    range_ =
        util::byte_range{position_for_next_artificial_rotate_, block_size_};
  } else {
    // Resuming from the beginning. Keep event_block_ in place - its event 0
    // IS the real FDE the client should see next, served by the main loop
    // after it drains artificial_rotate_pending_. Advance range_ past the
    // block we already have so the next fetch picks up where this one
    // ended.
    range_ = util::byte_range{opener_range.get_offset() +
                                  event_block_->get_actual_size(),
                              block_size_};
  }

  // From here on, every artificial ROTATE is for a storage-driven file
  // switch and always carries position=magic_binlog_offset.
  position_for_next_artificial_rotate_ = binsrv::events::magic_binlog_offset;
  // Future artificial ROTATEs follow the stream's own checksum setting.
  current_binlog_checksum_ = fde_checksum_enabled;
  return true;
}

[[nodiscard]] util::optional_bool
sender_context::populate_event_block(util::const_byte_span &event) {
  // Early reset to free memory from the previous event block.
  event_block_.reset();

  util::dynamic_byte_buffer buffer{};
  const binsrv::events::composite_binlog_name saved_binlog_name{binlog_name_};

  // On success both 'binlog_name_' and 'range_' will be updated.
  if (!storage_->fetch_event_block(binlog_name_, range_, buffer)) {
    return false;
  }

  const bool file_switched{!saved_binlog_name.is_empty() &&
                           binlog_name_ != saved_binlog_name};
  if (file_switched) {
    logger_->log_format(binsrv::log_severity::info,
                        "sender : switched to a new binlog file {} -> {}",
                        saved_binlog_name.str(), binlog_name_.str());
  }

  if (buffer.empty()) {
    logger_->log(binsrv::log_severity::info, "sender : fetched EOF");
    // storage::fetch_event_block set range_ to length 0 on EOF; restore the
    // length so the next polling attempt reads a full block at the same
    // offset. Leave binlog_name_ alone so we keep retrying the same file.
    range_ = util::byte_range{range_.get_offset(), block_size_};
    event_index_ = 0UZ;
    event = util::const_byte_span{};
    return true;
  }

  logger_->log_format(binsrv::log_severity::info,
                      "sender : fetched event block of size {}, {}:{}",
                      std::size(buffer), binlog_name_.str(),
                      range_.to_string());

  // Validate the first event's advertised size. If the fetched block cannot
  // even hold the common header, the stream is corrupt. If it holds the
  // header but not the full first event, re-fetch at the exact size.
  if (std::size(buffer) <
      binsrv::events::common_header_view_base::size_in_bytes) {
    return false;
  }
  const binsrv::events::common_header_view header{
      util::const_byte_span{buffer}.subspan(
          0UZ, binsrv::events::common_header_view_base::size_in_bytes)};
  const auto first_event_size{
      static_cast<std::size_t>(header.get_event_size_raw())};
  if (first_event_size > binsrv::events::max_event_size_bytes) {
    return false;
  }
  if (first_event_size > std::size(buffer)) {
    logger_->log_format(
        binsrv::log_severity::info,
        "sender : block too small for first event (needs {} bytes), "
        "retrying with exact size at {}:{}",
        first_event_size, binlog_name_.str(), range_.to_string());

    buffer.clear();
    range_ = util::byte_range{range_.get_offset(), first_event_size};
    if (!storage_->fetch_event_block(binlog_name_, range_, buffer)) {
      return false;
    }
  }

  event_block_ =
      std::make_unique<binsrv::indexed_event_block>(std::move(buffer));
  if (event_block_->is_empty()) {
    return false;
  }
  event_index_ = 0UZ;
  logger_->log_format(
      binsrv::log_severity::info,
      "sender : parsed event block with {} events, actual size {} byte(s)",
      event_block_->get_number_of_events(), event_block_->get_actual_size());

  range_ = util::byte_range{
      range_.get_offset() + event_block_->get_actual_size(), block_size_};

  if (file_switched) {
    // event_block_[0] is the real FDE of the new file; use it to generate
    // the file-switch artificial ROTATE that must precede it on the wire.
    enqueue_file_switch_rotate();
  }
  return {};
}

void sender_context::enqueue_file_switch_rotate() {
  assert(event_block_);
  assert(!event_block_->is_empty());
  const auto fde_bytes{event_block_->get_event(0)};
  const binsrv::events::common_header_view fde_header{fde_bytes.subspan(
      0UZ, binsrv::events::common_header_view_base::size_in_bytes)};
  // In a well-formed binlog file event 0 is always the FDE; log and bail
  // if that is not the case rather than generating a malformed ROTATE.
  if (fde_header.get_type_code() !=
      binsrv::events::code_type::format_description) {
    logger_->log(binsrv::log_severity::error,
                 "sender : first event of new binlog file is not an FDE");
    return;
  }
  std::uint32_t fde_server_id{};
  bool fde_checksum_enabled{};
  extract_fields_from_fde(fde_bytes, fde_server_id, fde_checksum_enabled);

  // Per MySQL protocol: a file-switch ROTATE's checksum follows the OLD
  // file's checksum (the one carried in current_binlog_checksum_ at this
  // point). AFTER the ROTATE is generated, update current_binlog_checksum_
  // from the NEW file's FDE so subsequent file-switch ROTATEs follow this
  // one.
  generate_rotate_event_ex(artificial_rotate_, current_binlog_checksum_,
                           binsrv::events::magic_binlog_offset,
                           false /* zero timestamp */, fde_server_id,
                           true /* artificial */, binlog_name_,
                           binsrv::events::magic_binlog_offset);
  artificial_rotate_pending_ = true;
  current_binlog_checksum_ = fde_checksum_enabled;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void sender_context::extract_fields_from_fde(util::const_byte_span fde_bytes,
                                             std::uint32_t &server_id,
                                             bool &checksum_enabled) const {
  const auto fde_ctx{make_fde_only_context()};
  const binsrv::events::event_view fde_v{fde_ctx, fde_bytes};
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

void sender_context::transform_fde_from(
    util::const_byte_span source_fde_bytes) {
  // Copy the on-disk FDE into our writable buffer, then zero the two header
  // fields a real MySQL master zeros on an artificial FDE
  // (next_event_position in the common header, create_timestamp in the
  // post header). The write_proxy destructor recalculates and writes the
  // CRC in the footer; the FDE always carries one regardless of body's
  // checksum_algorithm.
  transformed_fde_.assign(std::cbegin(source_fde_bytes),
                          std::cend(source_fde_bytes));
  const auto fde_ctx{make_fde_only_context()};
  const util::byte_span fde_span{transformed_fde_};
  const binsrv::events::event_updatable_view fde_uv{fde_ctx, fde_span};
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

} // namespace operations
