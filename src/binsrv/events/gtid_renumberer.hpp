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

#ifndef BINSRV_EVENTS_GTID_RENUMBERER_HPP
#define BINSRV_EVENTS_GTID_RENUMBERER_HPP

#include "binsrv/events/gtid_renumberer_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <optional>
#include <utility>

#include "binsrv/events/event_fwd.hpp"
#include "binsrv/events/event_view_fwd.hpp"

namespace binsrv::events {

// Rewrites GTID_LOG_EVENT, ANONYMOUS_GTID_LOG_EVENT and
// GTID_TAGGED_LOG_EVENT events so that their (sequence_number,
// last_committed) "logical clock" pair is consistent with the local
// binlog file boundaries produced by the rewrite-mode storage layer.
//
// Background:
//   In MySQL, the per-transaction (sequence_number, last_committed)
//   tuple drives the multi-threaded applier's intra-file parallelism.
//   The values are PER-FILE: every binlog file restarts sequence_number
//   from 1 and dependencies in last_committed reference sequence_numbers
//   from the same file only.
//
//   In rewrite mode the binlog server discards the source's ROTATE,
//   FORMAT_DESCRIPTION_EVENT, PREVIOUS_GTIDS_LOG, and STOP events and
//   coalesces transactions into local files at a configured size. Two
//   issues result:
//     1. A source-side rotation we discard does NOT reset our local
//        numbering, but the source has already restarted sequence_number
//        from 1 and re-used small numbers as last_committed; we'd end up
//        with collisions/duplicates that confuse the applier.
//     2. A local rotation we initiate splits the source's (still-running)
//        sequence_number sequence across two of OUR files; the second
//        file would start with sequence_number > 1, which is illegal.
//
// Approach:
//   For every GTID event we compute a per-event offset
//     offset = new_local_seq - source_seq
//   and use it to translate last_committed:
//     new_local_seq      = next monotonically growing local counter
//     new_last_committed = source_lc + offset      (in range)
//                        | new_local_seq - 1       (out of range, OR
//                        |                          first event of a
//                        |                          source-file
//                        |                          segment in this
//                        |                          local file)
//                        | 0                       (source_lc == 0
//                        |                          AND not a segment
//                        |                          boundary)
//
//   Why the offset trick works for in-segment events:
//     (a) source_seq is monotonic and gap-free within a single source
//         binlog file (incremented by 1 per binlogged transaction).
//     (b) last_committed in any GTID event only references a
//         sequence_number from the SAME source binlog file as the
//         event itself (never across files).
//   Plus by construction new_local_seq is also monotonic and gap-free.
//   From (a) the offset stays constant for the duration of a single
//   source file, and from (b) source_lc is guaranteed to live in that
//   same source file's seq space - so adding the current event's
//   offset to source_lc lands exactly on the local_seq we already
//   issued for the dependency target.
//
//   Why the FIRST event of each source-file segment needs special
//   handling:
//     The MySQL applier treats binlog file boundaries as implicit
//     synchronization points: when the source rotates from file A to
//     file B, all of A's transactions must commit before any of B's
//     start applying. The source encodes that by RESETTING its
//     logical clock at the boundary, so B's first event has
//     last_committed == 0 ("no in-file dependency"). That zero is
//     correct only as long as the file boundary is physically
//     preserved on the consumer side. In rewrite mode we coalesce
//     A's and B's events into a single local file, eliminating that
//     physical boundary - so emitting last_committed == 0 verbatim
//     would let the local applier reorder B's first commit BEFORE
//     A's last commit, violating source ordering. To restore the
//     barrier we set the first event of each new source-file segment
//     to last_committed = new_local_seq - 1, which transitively
//     chains through prior local txns and forces the segment to
//     commit after everything that came before it in the local file.
//     Subsequent events in the segment translate normally via the
//     offset.
//
//   Detecting "first event of a new segment":
//     We track the offset of the most recently emitted event. A new
//     event whose offset DIFFERS from that tracked offset is by
//     invariant (a) the start of a new source-file segment (or, if
//     the tracked offset is absent because of a local rotation or
//     fresh start, simply the start of the local file - which is
//     also the first event of a segment). No FDE / source-ROTATE
//     observation is required.
//
//   Out-of-range dependencies (candidate <= 0 inside a segment) come
//   from a dependency target that lived in an earlier (now-closed)
//   local file, or in the source's pre-attach history. Same fallback
//   - over-serialize against new_local_seq - 1 - which is always
//   conservative.
//
//   No (source_seq -> local_seq) history map is needed; the state is
//   the high-water mark `next_local_seq_` plus the most recently
//   tracked offset.
//
// Untagged GTID events have a fixed-layout 42-byte post header; the two
// 8-byte fields are patched in place and the event size does not change.
//
// Tagged GTID events store the same fields as variable-length integers
// inside a TLV body. Mutating the values may change the encoded body
// size, which in turn requires updating transaction_length (also a
// varlen integer in the same body, and self-referential because it
// names the current event's own size). The fixpoint converges in a
// handful of iterations (varlen widths are bounded to {1,2,3,4,5,9}).
class gtid_renumberer {
public:
  gtid_renumberer() = default;

  gtid_renumberer(const gtid_renumberer &) = delete;
  gtid_renumberer(gtid_renumberer &&) = delete;
  gtid_renumberer &operator=(const gtid_renumberer &) = delete;
  gtid_renumberer &operator=(gtid_renumberer &&) = delete;
  ~gtid_renumberer() = default;

  // Resets the per-file sequence_number counter to 0 so the next GTID
  // event we observe is renumbered starting from local_seq == 1. Call
  // this whenever a fresh local binlog file is opened (matching the
  // moment the storage layer emits its own ROTATE_EVENT +
  // FORMAT_DESCRIPTION_EVENT pair).
  void on_local_rotation() noexcept;

  // Restores the renumberer state when resuming an existing local
  // binlog file across a reconnect or process restart. Sets
  // next_local_seq_ to `next_local_seq` (the highest sequence_number
  // already emitted into that file) so the next GTID event we observe
  // is allocated as `next_local_seq + 1`. `last_emitted_offset` is the
  // tracked offset at the time of the last persisted snapshot; passing
  // std::nullopt forces the next event to be treated as a segment
  // boundary, which is conservative (forces last_committed = new_seq -
  // 1 once for the first post-resume transaction). Must be called
  // before any rewrite_if_gtid_event() call on this instance, i.e.
  // while the renumberer is still in its default-constructed state.
  // Also seeds the committed snapshot to the same value, so that an
  // immediate rollback_to_committed() (e.g. on a connection drop
  // before any user transaction is processed) restores the resumed
  // state rather than the default-constructed one.
  void resume_in_existing_local_file(
      std::int64_t next_local_seq,
      std::optional<std::int64_t> last_emitted_offset) noexcept;

  // Promotes the current (speculative) state to the committed
  // snapshot. Must be called at every transaction boundary - after a
  // GTID event has been rewritten and the rest of its transaction has
  // been buffered, but before the storage layer flushes anything to
  // disk - so that the persisted recovery snapshot (read back from
  // peek_*()) is in lockstep with the bytes that hit storage.
  // Idempotent: with no advancement since the last commit it is a
  // no-op.
  void commit_pending_changes() noexcept;

  // Reverts the speculative state to the most recent committed
  // snapshot. Used by the streaming pipeline when the storage layer
  // discards an in-flight transaction whose GTID event already
  // advanced the renumberer (e.g. after a mid-transaction
  // disconnect): without this rollback the next reconnect would
  // re-allocate a sequence_number for the same source GTID,
  // skipping over the speculative one and breaking gap-freedom of
  // the local sequence_number stream.
  void rollback_to_committed() noexcept;

  // If `event_uv` is one of the GTID event types, rewrites it in place
  // (or via a buffer-resize for GTID_TAGGED_LOG_EVENT) so the logical
  // clock matches our local file boundaries; otherwise returns
  // `event_uv` unchanged.
  //
  // For the tagged variant the call may resize `buffer` (the event's
  // backing storage). The returned view always references `buffer`.
  // For non-tagged events the buffer is untouched and the returned
  // view is the same one passed in.
  [[nodiscard]] event_updatable_view
  rewrite_if_gtid_event(const event_updatable_view &event_uv,
                        event_storage &buffer);

  // Snapshot accessors used by the storage layer to persist the
  // renumberer state alongside other per-binlog metadata. Together
  // they describe enough state to seed
  // resume_in_existing_local_file() on a subsequent process
  // start / reconnect.
  //
  // Both accessors return the COMMITTED snapshot (i.e. the state at
  // the most recently completed transaction boundary) rather than
  // the speculative in-flight state. This is intentional: if a
  // mid-transaction disconnect causes the storage layer to discard
  // the partial transaction, the persisted recovery info must
  // describe the durable bytes - not the optimistic increment that
  // was about to land before the drop. The translation logic
  // continues to operate on the speculative pair
  // (next_local_seq_, last_emitted_offset_) and is reconciled with
  // the committed snapshot via commit_pending_changes() /
  // rollback_to_committed().
  [[nodiscard]] std::int64_t peek_next_local_seq() const noexcept {
    return committed_next_local_seq_;
  }
  [[nodiscard]] std::optional<std::int64_t>
  peek_last_emitted_offset() const noexcept {
    return committed_last_emitted_offset_;
  }

private:
  // Allocates a fresh local sequence_number for the incoming GTID
  // event and computes the corresponding last_committed translation.
  // See class-level comment for the offset-based derivation; the
  // method does not retain any per-event state beyond bumping
  // next_local_seq_.
  [[nodiscard]] std::pair<std::int64_t, std::int64_t>
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  translate_logical_clock(std::int64_t source_seq,
                          std::int64_t source_lc) noexcept;

  // Patches sequence_number / last_committed in place inside an
  // already-materialized GTID_LOG_EVENT or ANONYMOUS_GTID_LOG_EVENT.
  // The event size and overall buffer size are unchanged. Checksum
  // recalculation happens via the existing write_proxy mechanism.
  void rewrite_untagged_in_place(const event_updatable_view &event_uv);

  // Decodes the tagged GTID body, mutates the renumbered fields and
  // re-encodes into `buffer` (which is resized as needed). Returns a
  // fresh event_updatable_view referencing `buffer`.
  [[nodiscard]] event_updatable_view
  rewrite_tagged(const event_updatable_view &event_uv, event_storage &buffer);

  // Monotonic counter representing the highest local sequence_number
  // emitted so far in the current local binlog file. Initialized to 0
  // (no transactions yet) so that pre-increment yields 1 for the very
  // first transaction (matching MySQL's "sequence_number starts at 1
  // in every binlog file" invariant). Reset to 0 by
  // on_local_rotation().
  std::int64_t next_local_seq_{0};

  // Offset (= new_local_seq - source_seq) that was active for the most
  // recently emitted GTID event in the current local binlog file. If
  // the upcoming event has a different offset we are crossing a
  // source-file segment boundary and must serialize the event against
  // the most recent local txn (see class-level comment). std::nullopt
  // means "no event has been emitted in the current local file yet";
  // it is reset to nullopt by on_local_rotation() and indistinguishable
  // from a segment boundary for translation purposes.
  std::optional<std::int64_t> last_emitted_offset_{};

  // Committed snapshot of the (next_local_seq_, last_emitted_offset_)
  // pair, advanced only at transaction boundaries via
  // commit_pending_changes(). It is the value persisted into the
  // per-binlog .json metadata (see peek_*()) and the value
  // restored by rollback_to_committed() when the storage layer drops
  // an incomplete transaction. Both fields share the same lifecycle
  // as their speculative counterparts: zeroed by on_local_rotation()
  // and seeded by resume_in_existing_local_file().
  std::int64_t committed_next_local_seq_{0};
  std::optional<std::int64_t> committed_last_emitted_offset_{};
};

} // namespace binsrv::events

#endif // BINSRV_EVENTS_GTID_RENUMBERER_HPP
