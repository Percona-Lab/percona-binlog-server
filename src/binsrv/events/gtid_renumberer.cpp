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

#include "binsrv/events/gtid_renumberer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>

// `event_storage` (used by `rewrite_tagged_gtid_event()` below via the
// `assign()` member) is an alias for boost::container::small_vector<>,
// declared in event_fwd.hpp via container_fwd.hpp - so we need the full
// definition here in the .cpp.
#include <boost/container/small_vector.hpp> // IWYU pragma: keep

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_header_view.hpp"
#include "binsrv/events/event_fwd.hpp"
#include "binsrv/events/event_view.hpp"
#include "binsrv/events/gtid_log_post_header.hpp"
#include "binsrv/events/gtid_tagged_log_body_impl.hpp"

#include "util/byte_span.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::events {

namespace {

// Defensive cap on the number of fixpoint iterations needed to converge
// on the tagged-event transaction_length. Each iteration changes the
// encoded width of either serializable_field_size or transaction_length,
// both of which are bounded to {1,2,3,4,5,9} bytes; in practice 2-3
// iterations are sufficient. The cap is set high enough that it can only
// trigger in genuine logic bugs.
constexpr int max_tagged_fixpoint_iterations{16};

} // namespace

void gtid_renumberer::on_local_rotation() noexcept {
  next_local_seq_ = 0;
  last_emitted_offset_.reset();
  committed_next_local_seq_ = 0;
  committed_last_emitted_offset_.reset();
}

void gtid_renumberer::resume_in_existing_local_file(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::int64_t next_local_seq,
    std::optional<std::int64_t> last_emitted_offset) noexcept {
  next_local_seq_ = next_local_seq;
  last_emitted_offset_ = last_emitted_offset;
  committed_next_local_seq_ = next_local_seq;
  committed_last_emitted_offset_ = last_emitted_offset;
}

void gtid_renumberer::commit_pending_changes() noexcept {
  committed_next_local_seq_ = next_local_seq_;
  committed_last_emitted_offset_ = last_emitted_offset_;
}

void gtid_renumberer::rollback_to_committed() noexcept {
  next_local_seq_ = committed_next_local_seq_;
  last_emitted_offset_ = committed_last_emitted_offset_;
}

[[nodiscard]] std::pair<std::int64_t, std::int64_t>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
gtid_renumberer::translate_logical_clock(std::int64_t source_seq,
                                         std::int64_t source_lc) noexcept {
  // Allocate a fresh local sequence_number for this transaction.
  ++next_local_seq_;
  const std::int64_t new_seq{next_local_seq_};
  const std::int64_t offset{new_seq - source_seq};

  // Translate last_committed.
  //
  // The general rule is "candidate = source_lc + offset", which works
  // because within a single source file the offset stays constant
  // (both source_seq and new_seq advance by exactly 1 per event) and
  // last_committed always references a sequence_number from the same
  // source file. So adding the current event's offset to source_lc
  // lands exactly on the local_seq we previously issued for the
  // dependency target.
  //
  // There are two cases where that translation is not appropriate:
  //
  // 1) `last_emitted_offset_` differs from `offset` (or is empty).
  //    A different offset means the previous event and this one
  //    belong to different source-file segments in the local file
  //    (offset is invariant within a source file, so a change is
  //    proof of a boundary). On the source side that boundary is a
  //    synchronization point - all of the previous file's
  //    transactions must commit before any of the new file's start
  //    applying - and the source encodes that by RESETTING its
  //    logical clock so the first event has source_lc == 0. In the
  //    local file we no longer have a physical boundary, so emitting
  //    last_committed == 0 verbatim would let the local applier
  //    reorder this txn before the previous segment's last commit.
  //    To restore the barrier we set new_lc = (new_seq - 1), which
  //    transitively chains through prior local txns and forces this
  //    one to commit after everything that came before it. For the
  //    very first txn in the local file (new_seq == 1) the formula
  //    yields 0, i.e. parallel-safe - which is correct because there
  //    is no prior local txn to serialize against.
  //    The "empty last_emitted_offset_" case (fresh local file or
  //    just after on_local_rotation()) is handled identically; from
  //    the local file's point of view its first event is by
  //    definition the start of a new segment.
  //
  // 2) source_lc != 0 inside a segment but the candidate falls out
  //    of range (candidate <= 0). The dependency target is in an
  //    earlier (now-closed) local file or in the source's pre-attach
  //    history; we have no local_seq to point at, so we fall back to
  //    over-serializing against (new_seq - 1). candidate < new_seq
  //    is guaranteed by the protocol invariant source_lc <
  //    source_seq, so we only need to bound-check the lower edge.
  std::int64_t new_lc{0};
  const bool segment_boundary{!last_emitted_offset_.has_value() ||
                              *last_emitted_offset_ != offset};
  if (segment_boundary) {
    new_lc = new_seq - 1;
  } else if (source_lc != 0) {
    const std::int64_t candidate{source_lc + offset};
    new_lc = candidate > 0 ? candidate : new_seq - 1;
  }

  last_emitted_offset_ = offset;
  return {new_seq, new_lc};
}

void gtid_renumberer::rewrite_untagged_in_place(
    const event_updatable_view &event_uv) {
  // Untagged GTID events keep ALL the renumber-relevant data in the
  // 42-byte post header; the body holds timestamps / commit-ticket /
  // transaction_length but none of those depend on sequence_number or
  // last_committed. The event size therefore stays the same.
  const gtid_log_post_header decoded_post_header{
      event_uv.get_post_header_raw()};
  const auto [new_seq, new_lc]{
      translate_logical_clock(decoded_post_header.get_sequence_number_raw(),
                              decoded_post_header.get_last_committed_raw())};

  // The write_proxy automatically recomputes the footer CRC at scope
  // exit, so we patch under it.
  const auto proxy{event_uv.get_write_proxy()};
  overwrite_logical_clock_in_post_header_raw(
      proxy.get_post_header_updatable_raw(), new_lc, new_seq);
}

[[nodiscard]] event_updatable_view
gtid_renumberer::rewrite_tagged(const event_updatable_view &event_uv,
                                event_storage &buffer) {
  using tagged_body_type = generic_body_impl<code_type::gtid_tagged_log>;

  const std::size_t common_header_size{
      event_view_base::get_common_header_size()};
  const std::size_t footer_size{event_uv.get_footer_size()};
  const std::size_t old_event_size{event_uv.get_total_size()};

  // Sanity: tagged GTID events have a zero-byte post header.
  if (event_uv.get_post_header_size() != 0U) {
    util::exception_location().raise<std::logic_error>(
        "gtid_tagged_log event unexpectedly has a non-empty post header");
  }

  // Decode the body. The view (and therefore tagged_body_type's source
  // span) lives in `buffer`; we must finish reading before resizing.
  tagged_body_type body{event_uv.get_body_raw()};

  const auto [new_seq, new_lc]{translate_logical_clock(
      body.get_sequence_number_raw(), body.get_last_committed_raw())};
  body.set_sequence_number_raw(new_seq);
  body.set_last_committed_raw(new_lc);

  // Snapshot the original common header bytes BEFORE we touch the
  // buffer; the input view is invalidated by the resize below.
  std::array<std::byte, common_header_view_base::size_in_bytes>
      original_common_header{};
  const auto original_common_header_raw{event_uv.get_common_header_raw()};
  if (std::size(original_common_header_raw) !=
      std::size(original_common_header)) {
    util::exception_location().raise<std::logic_error>(
        "common header size mismatch while rewriting gtid_tagged_log event");
  }
  std::memcpy(std::data(original_common_header),
              std::data(original_common_header_raw),
              std::size(original_common_header));

  // Solve for transaction_length and the new event size in tandem.
  // trx_minus_event = total transaction size - this event's old size,
  // which is independent of how this event ends up encoded; the only
  // dependency that varies is the new event's size itself.
  const std::uint64_t old_transaction_length{body.get_transaction_length_raw()};
  if (old_transaction_length < old_event_size) {
    util::exception_location().raise<std::invalid_argument>(
        "transaction_length in gtid_tagged_log event is smaller than the "
        "event itself");
  }
  const std::uint64_t trx_minus_event{old_transaction_length - old_event_size};

  std::uint64_t guess_event_size{old_event_size};
  std::size_t new_body_size{0U};
  for (int attempt{0}; attempt < max_tagged_fixpoint_iterations; ++attempt) {
    body.set_transaction_length_raw(trx_minus_event + guess_event_size);
    new_body_size = body.calculate_encoded_size();
    const std::uint64_t candidate_event_size{common_header_size +
                                             new_body_size + footer_size};
    if (candidate_event_size == guess_event_size) {
      break;
    }
    guess_event_size = candidate_event_size;
  }
  // If we didn't converge, body's transaction_length is now stale; the
  // last loop iteration's calculate_encoded_size() does not match the
  // value embedded in the body. That would only happen on a logic bug
  // (e.g. calculate_varlen_int_size disagreeing with the actual encoded
  // width); refuse to emit a malformed event.
  if (body.get_transaction_length_raw() != trx_minus_event + guess_event_size) {
    util::exception_location().raise<std::logic_error>(
        "gtid_tagged_log transaction_length fixpoint did not converge");
  }

  const std::size_t new_event_size{static_cast<std::size_t>(guess_event_size)};

  // Resize the destination buffer; this invalidates `event_uv` (and the
  // body's source span, which we no longer need).
  buffer.assign(new_event_size, std::byte{0});
  const util::byte_span destination{std::data(buffer), std::size(buffer)};

  // 1) Common header: copy the original 19 bytes verbatim, then patch
  //    event_size to reflect the new total. next_event_position is set
  //    by the caller (which knows the storage cursor) AFTER this call
  //    returns; we leave the original value here and let the caller
  //    overwrite it as it does for the non-rewritten case.
  std::memcpy(std::data(destination), std::data(original_common_header),
              std::size(original_common_header));

  // 2) Body: encode_to() advances `body_dest` past the bytes it wrote.
  util::byte_span body_dest{
      destination.subspan(common_header_size, new_body_size)};
  body.encode_to(body_dest);
  if (!body_dest.empty()) {
    util::exception_location().raise<std::logic_error>(
        "gtid_tagged_log body encoder wrote fewer bytes than reported by "
        "calculate_encoded_size()");
  }

  // 3) Footer (4 bytes if present): leave zero-filled; the caller's
  //    write_proxy will recalculate the CRC.

  // Build a fresh writable view; bypass the reader_context-driven
  // validation because we know the structure first-hand.
  auto result{event_updatable_view::from_raw_unchecked(
      destination, /*post_header_size=*/0U, footer_size)};

  // Update event_size in the freshly-laid-out common header.
  {
    const auto proxy{result.get_write_proxy()};
    proxy.get_common_header_updatable_view().set_event_size_raw(
        static_cast<std::uint32_t>(new_event_size));
  }
  return result;
}

[[nodiscard]] event_updatable_view
gtid_renumberer::rewrite_if_gtid_event(const event_updatable_view &event_uv,
                                       event_storage &buffer) {
  const auto code{event_uv.get_common_header_view().get_type_code()};
  switch (code) {
  case code_type::gtid_log:
  case code_type::anonymous_gtid_log:
    rewrite_untagged_in_place(event_uv);
    return event_uv;
  case code_type::gtid_tagged_log:
    return rewrite_tagged(event_uv, buffer);
  default:
    return event_uv;
  }
}

} // namespace binsrv::events
