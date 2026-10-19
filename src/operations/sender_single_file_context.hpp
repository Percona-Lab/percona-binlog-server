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

#ifndef OPERATIONS_SENDER_SINGLE_FILE_CONTEXT_HPP
#define OPERATIONS_SENDER_SINGLE_FILE_CONTEXT_HPP

#include "operations/sender_single_file_context_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <cstdint>

// needed for 'event_storage'
#include <boost/container/small_vector.hpp> // IWYU pragma: keep

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/indexed_event_block_fwd.hpp"
#include "binsrv/storage_core_fwd.hpp"
#include "binsrv/storage_fwd.hpp"

#include "binsrv/events/composite_binlog_name.hpp"
#include "binsrv/events/event_fwd.hpp"

#include "util/byte_range.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/common_optional_types.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"

namespace operations {

class sender_single_file_context {
public:
  static constexpr std::size_t min_allowed_block_size{4096UZ};

  // deliberately passing by value as we will be moving from these objects
  sender_single_file_context(binsrv::basic_logger_ptr logger,
                             binsrv::storage_ptr storage,
                             std::size_t block_size,
                             binsrv::events::composite_binlog_name binlog_name,
                             std::uint64_t position, bool checksum_enabled);

  sender_single_file_context(const sender_single_file_context &) = delete;
  sender_single_file_context &
  operator=(const sender_single_file_context &) = delete;
  sender_single_file_context(sender_single_file_context &&) = delete;
  sender_single_file_context &operator=(sender_single_file_context &&) = delete;
  ~sender_single_file_context();

  [[nodiscard]] bool
  is_checksum_enabled_in_format_description() const noexcept {
    return extracted_checksum_enabled_;
  }
  // returns 'success' and sets the event span to a non-empty value on success
  // returns 'eof' and sets the event span to an empty object on EOF of a
  //         non-active (already closed) file
  // returns 'waiting' and sets the event span to an empty object when no
  //         more data is available yet in an active (currently being
  //         updated) file
  // throws on an error, including being called again after 'eof'
  [[nodiscard]] event_fetching_result_type
  get_event(util::const_byte_span &event);

  // Assembles a HEARTBEAT_LOG_EVENT_V2 describing the position reached so far
  // in this binlog file (the point a blocking dump sits idle at) and sets
  // 'event' to point at it. The event carries the same file name, server_id
  // and checksum flag as the surrounding stream. Returns true when an event was
  // produced; returns false with an empty 'event' when the preamble has not
  // been consumed yet, so there is no valid position to report - the caller
  // should keep waiting rather than treat this as an error.
  [[nodiscard]] bool generate_heartbeat(util::const_byte_span &event);

private:
  enum class fsm_state_type : std::uint8_t {
    artificial_rotate,
    format_description,
    previous_gtids_log,
    main_loop,
    finish
  };
  // construction-time parameters
  binsrv::basic_logger_ptr logger_{};
  binsrv::storage_ptr storage_{};
  std::size_t construction_block_size_{};
  binsrv::events::composite_binlog_name construction_binlog_name_{};
  std::uint64_t construction_position_{};
  bool construction_checksum_enabled_{};

  // space for preamble events
  binsrv::events::event_storage generated_artificial_rotate_{};
  binsrv::events::event_storage extracted_format_description_{};
  binsrv::events::event_storage extracted_previous_gtids_log_{};

  // space for the heartbeat events generated while idle at EOF
  binsrv::events::event_storage generated_heartbeat_{};

  // cached parameters extracted from the format description event
  bool extracted_checksum_enabled_{};
  std::uint32_t extracted_server_id_{};

  // internal state for iteration within 'get_event()'
  fsm_state_type fsm_state_{fsm_state_type::artificial_rotate};
  std::uint64_t current_position_{};
  std::size_t event_index_{};
  binsrv::indexed_event_block_ptr event_block_{};

  // internal state for managing the carry buffer
  util::dynamic_byte_buffer carry_buffer_{};
  std::uint64_t carry_buffer_position_{};

  void discard_from_carry_buffer(std::size_t size);
  void assign_carry_buffer(util::const_byte_span data,
                           std::uint64_t carry_buffer_position);
  // Consumes a matching carry prefix and fetches only the missing suffix.
  // EOF after a non-empty prefix is reported as success_truncated.
  [[nodiscard]] binsrv::storage_fetch_result_type
  fetch_with_carry_buffer(const util::byte_range &range,
                          util::dynamic_byte_buffer &buffer);

  [[nodiscard]] static std::size_t
  calculate_fde_size(std::uint32_t encoded_server_version);
  void process_format_description(bool transform_to_artificial);
  void process_previous_gtids();
  void generate_artificial_rotate();

  [[nodiscard]] event_fetching_result_type populate_preamble();
  [[nodiscard]] event_fetching_result_type populate_event_block();
};

} // namespace operations

#endif // OPERATIONS_SENDER_SINGLE_FILE_CONTEXT_HPP
