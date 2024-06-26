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

#include "binsrv/event/reader_context.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "binsrv/event/checksum_algorithm_type.hpp"
#include "binsrv/event/code_type.hpp"
#include "binsrv/event/event.hpp"
#include "binsrv/event/flag_type.hpp"
#include "binsrv/event/protocol_traits.hpp"

#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

reader_context::reader_context(checksum_algorithm_type checksum_algorithm)
    : checksum_algorithm_{checksum_algorithm},
      post_header_lengths_{event::expected_post_header_lengths} {}

void reader_context::process_event(const event &current_event) {
  bool processed{false};
  while (!processed) {
    switch (state_) {
    case state_type::initial:
      processed = process_event_in_initial_state(current_event);
      break;
    case state_type::rotate_artificial_processed:
      processed =
          process_event_in_rotate_artificial_processed_state(current_event);
      break;
    case state_type::format_description_processed:
      processed =
          process_event_in_format_description_processed_state(current_event);
      break;
    default:
      assert(false);
    }
  }
  // TODO: check if CRC32 checksum from the footer (if present) matches the
  //       calculated one
}

[[nodiscard]] bool
reader_context::process_event_in_initial_state(const event &current_event) {
  assert(state_ == state_type::initial);
  const auto &common_header{current_event.get_common_header()};

  // in the "initial" state we expect only artificial rotate events
  const auto is_artificial{
      common_header.get_flags().has_element(flag_type::artificial)};
  const auto is_artificial_rotate{
      common_header.get_type_code() == code_type::rotate && is_artificial};
  if (!is_artificial_rotate) {
    util::exception_location().raise<std::logic_error>(
        "an artificial rotate event must be the very first event in the "
        "initial state");
  }

  // artificial rotate events must always have next event position and
  // timestamp set to 0
  if (common_header.get_timestamp_raw() != 0U) {
    util::exception_location().raise<std::logic_error>(
        "non-zero timestamp found in an artificial rotate event");
  }
  const auto is_pseudo{common_header.get_next_event_position_raw() == 0U};
  if (!is_pseudo) {
    util::exception_location().raise<std::logic_error>(
        "non-zero next event position found in an artificial rotate event");
  }

  // we expect an artificial rotate event to be the very first event in the
  // newly-created binlog file
  if (position_ != 0U) {
    util::exception_location().raise<std::logic_error>(
        "artificial rotate event is not the very first event in a binlog "
        "file");
  }

  position_ = static_cast<std::uint32_t>(
      current_event.get_post_header<code_type::rotate>().get_position_raw());

  // transition to the next state
  state_ = state_type::rotate_artificial_processed;
  return true;
}

[[nodiscard]] bool
reader_context::process_event_in_rotate_artificial_processed_state(
    const event &current_event) {
  assert(state_ == state_type::rotate_artificial_processed);
  const auto &common_header{current_event.get_common_header()};

  // in the "rotate_artificial_processed" state we expect only format
  // description events
  if (common_header.get_type_code() != code_type::format_description) {
    // TODO: this check should be performed just after the common header is
    //       parsed to make sure we rely on proper post_header lengths
    util::exception_location().raise<std::logic_error>(
        "format description event must follow an artificial rotate event");
  }

  const auto &post_header{
      current_event.get_post_header<code_type::format_description>()};

  // check if FDE has expected binlog version number
  if (post_header.get_binlog_version_raw() != default_binlog_version) {
    util::exception_location().raise<std::logic_error>(
        "unexpected binlog version number in format description event");
  }

  // check if FDE has expected common header size
  if (post_header.get_common_header_length() != default_common_header_length) {
    util::exception_location().raise<std::logic_error>(
        "unexpected common header length in format description event");
  }

  // check if the values from the post_header_lengths array are the same as
  // generic_post_header_impl<code_type::xxx>::size_in_bytes for known events
  validate_post_header_lengths(post_header.get_post_header_lengths_raw(),
                               event::expected_post_header_lengths);

  post_header_lengths_ = post_header.get_post_header_lengths_raw();

  const auto &body{current_event.get_body<code_type::format_description>()};
  checksum_algorithm_ = body.get_checksum_algorithm();

  // some format description events (non-pseudo ones) must be written to
  // the binary log file and advance position when being processed
  const auto is_pseudo{common_header.get_next_event_position_raw() == 0U};
  if (!is_pseudo) {
    validate_position_and_advance(common_header);
  }
  // transition to the next state
  state_ = state_type::format_description_processed;
  return true;
}

[[nodiscard]] bool
reader_context::process_event_in_format_description_processed_state(
    const event &current_event) {
  assert(state_ == state_type::format_description_processed);
  const auto &common_header{current_event.get_common_header()};
  const auto code{common_header.get_type_code()};
  const auto is_artificial{
      common_header.get_flags().has_element(flag_type::artificial)};

  // early return here with "false" return code so that the while loop
  // in the main 'process_event()' method would repeat processing this
  // event but from the initial state (which is set here)
  if (code == code_type::rotate && is_artificial) {
    position_ = 0U;
    state_ = state_type::initial;
    return false;
  }

  if (code == code_type::format_description) {
    // format description event must appear only once within a binlog
    util::exception_location().raise<std::logic_error>(
        "encountered second format description event within the same binlog");
  }

  if (is_artificial) {
    util::exception_location().raise<std::logic_error>(
        "unexpected artificial event in the 'format description processed' "
        "state");
  }

  const auto is_pseudo{common_header.get_next_event_position_raw() == 0U};
  if (is_pseudo) {
    util::exception_location().raise<std::logic_error>(
        "unexpected pseudo event in the 'format description processed' state");
  }

  switch (code) {
  case code_type::rotate:
    // here we process only real (non-artificial) rotate events because of
    // the early return at the beginning of the method
    assert(!is_artificial);
    // position in non-artificial rotate event post header must be equal to
    // magic_binlog_offset (4)
    if (current_event.get_post_header<code_type::rotate>().get_position_raw() !=
        magic_binlog_offset) {
      util::exception_location().raise<std::logic_error>(
          "unexpected position in an non-artificial rotate event post "
          "header");
    }
    [[fallthrough]];
  case code_type::stop:
    // the last event in binlog could be not only a non-artificial
    // ROTATE (in case when admin executed FLUSH BINARY LOGS) but STOP (when
    // the server was shut down)
    // in this latter case, we also reset the position in order to indicate
    // the end of the current cycle and expect new ROTATE(artificial) and
    // FORMAT_DESCRIPTION
    position_ = 0U;
    state_ = state_type::initial;
    break;
  default:
    validate_position_and_advance(common_header);
    // not changing the state as we remain in 'format_description_processed'
  }
  return true;
}

void reader_context::validate_position_and_advance(
    const common_header &common_header) {
  assert(common_header.get_next_event_position_raw() != 0U);
  // check if common_header.next_event_position matches current position
  // plus common_header.event_size
  if (position_ + common_header.get_event_size_raw() !=
      common_header.get_next_event_position_raw()) {
    util::exception_location().raise<std::logic_error>(
        "unexpected next event position in the event common header");
  }
  // simply advance current position
  position_ = common_header.get_next_event_position_raw();
}

[[nodiscard]] checksum_algorithm_type
reader_context::get_current_checksum_algorithm() const noexcept {
  return checksum_algorithm_;
}

[[nodiscard]] std::size_t
reader_context::get_current_post_header_length(code_type code) const noexcept {
  return get_post_header_length_for_code(post_header_lengths_, code);
}

} // namespace binsrv::event
