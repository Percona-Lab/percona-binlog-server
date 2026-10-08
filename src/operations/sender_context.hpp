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

#ifndef OPERATIONS_SENDER_CONTEXT_HPP
#define OPERATIONS_SENDER_CONTEXT_HPP

#include "operations/sender_context_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <cstdint>

// needed for 'event_storage'
#include <boost/container/small_vector.hpp> // IWYU pragma: keep

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/indexed_event_block_fwd.hpp"
#include "binsrv/storage_fwd.hpp"

#include "binsrv/events/composite_binlog_name.hpp"
#include "binsrv/events/event_fwd.hpp"

#include "util/byte_range.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/common_optional_types.hpp"

namespace operations {

class sender_context {
public:
  // deliberately passing by value as we will be moving from these objects
  sender_context(binsrv::basic_logger_ptr logger, binsrv::storage_ptr storage,
                 std::size_t block_size, std::string_view binlog_name,
                 std::uint64_t position, bool session_source_binlog_checksum);

  sender_context(const sender_context &) = delete;
  sender_context &operator=(const sender_context &) = delete;
  sender_context(sender_context &&) = delete;
  sender_context &operator=(sender_context &&) = delete;
  ~sender_context();

  // returns false on error
  // returns true and sets the event span to a non-empty value on success
  // returns true and sets the event span to an empty object on EOF
  [[nodiscard]] bool get_event(util::const_byte_span &event);

private:
  binsrv::basic_logger_ptr logger_{};
  binsrv::storage_ptr storage_{};

  std::size_t block_size_{};

  // Checksum of the most recently seen FDE (or the session-level setting
  // before the first FDE has been seen). Decides whether the next artificial
  // ROTATE we generate carries a CRC32 footer.
  bool current_binlog_checksum_{};

  // Position field of the next artificial ROTATE to generate. On
  // construction this is the client-requested start position; after the
  // opener is sent it is permanently reset to magic_binlog_offset for the
  // file-switch ROTATEs that follow.
  std::uint64_t position_for_next_artificial_rotate_{};

  binsrv::events::composite_binlog_name binlog_name_{};
  util::byte_range range_{};
  binsrv::indexed_event_block_ptr event_block_{};
  std::size_t event_index_{};

  // Opener bookkeeping. On the very first get_event() call we fetch a block
  // at offset 4 of the resolved file, pull the FDE out of event 0 and stash
  // one or two artificial events to send before any real event flows out.
  // On a storage-driven file switch we stash exactly one more artificial
  // ROTATE. Each stashed event lives in its own event_storage so the
  // util::const_byte_span the caller gets back from get_event() stays valid
  // until the next get_event() call.
  bool opener_initialized_{false};
  bool artificial_rotate_pending_{false};
  bool transformed_fde_pending_{false};
  binsrv::events::event_storage artificial_rotate_{};
  binsrv::events::event_storage transformed_fde_{};

  [[nodiscard]] bool initialize_opener();
  [[nodiscard]] util::optional_bool
  populate_event_block(util::const_byte_span &event);
  void enqueue_file_switch_rotate();
  void extract_fields_from_fde(util::const_byte_span fde_bytes,
                               std::uint32_t &server_id,
                               bool &checksum_enabled) const;
  void transform_fde_from(util::const_byte_span source_fde_bytes);
};

} // namespace operations

#endif // OPERATIONS_SENDER_CONTEXT_HPP
