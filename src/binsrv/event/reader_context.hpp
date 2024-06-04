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

#ifndef BINSRV_EVENT_READER_CONTEXT_HPP
#define BINSRV_EVENT_READER_CONTEXT_HPP

#include "binsrv/event/reader_context_fwd.hpp" // IWYU pragma: export

#include "binsrv/event/checksum_algorithm_type_fwd.hpp"
#include "binsrv/event/common_header_fwd.hpp"
#include "binsrv/event/event_fwd.hpp"
#include "binsrv/event/protocol_traits.hpp"

namespace binsrv::event {

class [[nodiscard]] reader_context {
  friend class event;

public:
  explicit reader_context(checksum_algorithm_type checksum_algorithm);

  [[nodiscard]] checksum_algorithm_type
  get_current_checksum_algorithm() const noexcept;
  [[nodiscard]] std::size_t
  get_current_post_header_length(code_type code) const noexcept;
  [[nodiscard]] std::uint32_t get_current_position() const noexcept {
    return position_;
  }

private:
  // this class implements the logic of the following state machine
  // (ROTATE(artificial) FORMAT_DESCRIPTION <ANY>* (ROTATE|STOP)?)+
  enum class state_type {
    initial,
    rotate_artificial_processed,
    format_description_processed
  };
  state_type state_{state_type::initial};
  checksum_algorithm_type checksum_algorithm_;
  post_header_length_container post_header_lengths_{};
  std::uint32_t position_{0U};

  void process_event(const event &current_event);
  [[nodiscard]] bool process_event_in_initial_state(const event &current_event);
  [[nodiscard]] bool process_event_in_rotate_artificial_processed_state(
      const event &current_event);
  [[nodiscard]] bool process_event_in_format_description_processed_state(
      const event &current_event);
  void validate_position_and_advance(const common_header &common_header);
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_READER_CONTEXT_HPP
