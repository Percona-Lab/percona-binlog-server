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
#include "util/dynamic_byte_buffer_fwd.hpp"

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
  bool current_binlog_checksum_{};

  enum class fsm_state_type : std::uint8_t {
    start_from_beginning,
    start_from_offset,
    generate_artificial_fde,
    fetch_event_from_storage
  };
  fsm_state_type fsm_state_{};

  std::uint64_t position_for_artificial_rotate_{};
  binsrv::events::composite_binlog_name binlog_name_{};
  util::byte_range range_{};
  binsrv::events::event_storage artificial_rotate_{};
  binsrv::events::event_storage fde_{};
  util::dynamic_byte_buffer carry_buffer_{};
  binsrv::indexed_event_block_ptr event_block_{};
  std::size_t event_index_{};

  [[nodiscard]] static std::size_t
  calculate_fde_size(std::uint32_t encoded_server_version);
  [[nodiscard]] bool fetch_fde_from_storage();
  [[nodiscard]] bool extract_fde_from_event_block();
  void extract_fields_from_fde(std::uint32_t &server_id,
                               bool &checksum_enabled) const;
  void transform_fde_to_artificial();

  [[nodiscard]] util::optional_bool
  populate_event_block(util::const_byte_span &event);
  // Fetches the next block from storage, prepending any carried tail, and
  // parses it into 'event_block_' / resets 'event_index_'. Sets 'is_eof' to
  // true when storage reports no more data; in that case 'event_block_'
  // stays unset and any carried bytes remain in 'carry_buffer_' for the
  // next attempt. Returns false on fatal error.
  [[nodiscard]] bool load_next_event_block(bool &is_eof);
  [[nodiscard]] bool handle_start_states(util::const_byte_span &event);
  [[nodiscard]] bool
  handle_generate_artificial_fde_state(util::const_byte_span &event);
  [[nodiscard]] bool
  handle_fetch_event_from_storage_state(util::const_byte_span &event);
};

} // namespace operations

#endif // OPERATIONS_SENDER_CONTEXT_HPP
