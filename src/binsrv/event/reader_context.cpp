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

reader_context::reader_context()
    : checksum_algorithm_{checksum_algorithm_type::off} {}

void reader_context::process_event(const event &current_event) {
  const auto &common_header{current_event.get_common_header()};
  const auto code{common_header.get_type_code()};
  const auto is_artificial{
      common_header.get_flags().has_element(flag_type::artificial)};
  const auto is_pseudo{common_header.get_next_event_position_raw() == 0U};
  // artificial rotate events must always have enext event position and
  // timestamp setto 0
  if (code == code_type::rotate && is_artificial) {
    if (common_header.get_timestamp_raw() != 0U) {
      util::exception_location().raise<std::logic_error>(
          "non-zero timestamp found in an artificial rotate event");
    }
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
  }
  if (!is_artificial && !is_pseudo) {
    // every non-artificial event must be preceded by the FDE
    // (the exception is FDE itself)
    if (code != code_type::format_description && !fde_processed_) {
      // TODO: this check should be performed just after the common header is
      //       parsed to make sure we rely on proper post_header lengths
      util::exception_location().raise<std::logic_error>(
          "a non-artificial event encountered before format description event");
    }

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

  if (code == code_type::rotate && !is_artificial) {
    // position in non-artificial rotate event post header must be equal to
    // magic_binlog_offset (4)
    if (current_event.get_post_header<code_type::rotate>().get_position_raw() !=
        magic_binlog_offset) {
      util::exception_location().raise<std::logic_error>(
          "unexpected position in an non-artificial rotate event post "
          "header");
    }

    // normal (non-artificial) event is expected to be the last event in
    // binlog - so we reset the current position here
    position_ = 0U;
  }

  // TODO: add some kind of state machine where the expected sequence of events
  //       is the following -
  //       (ROTATE(artificial) FORMAT_DESCRIPTION <ANY>* ROTATE)*
  if (code == code_type::format_description) {
    const auto &post_header{
        current_event.get_post_header<code_type::format_description>()};
    const auto &body{current_event.get_body<code_type::format_description>()};

    // check if FDE has expected binlog version number
    if (post_header.get_binlog_version_raw() != default_binlog_version) {
      util::exception_location().raise<std::logic_error>(
          "unexpected binlog version number in format description event");
    }

    // check if FDE has expected common header size
    if (post_header.get_common_header_length() !=
        default_common_header_length) {
      util::exception_location().raise<std::logic_error>(
          "unexpected common header length in format description event");
    }

    // check if the values from the post_header_lengths array are the same as
    // generic_post_header_impl<code_type::xxx>::size_in_bytes for known events
    validate_post_header_lengths(post_header.get_post_header_lengths_raw(),
                                 event::expected_post_header_lengths);

    fde_processed_ = true;
    post_header_lengths_ = post_header.get_post_header_lengths_raw();
    checksum_algorithm_ = body.get_checksum_algorithm();
  }
  // TODO: check if CRC32 checksum from the footer (if present) matches the
  //       calculated one
}

[[nodiscard]] checksum_algorithm_type
reader_context::get_current_checksum_algorithm() const noexcept {
  assert(has_fde_processed());
  return checksum_algorithm_;
}

[[nodiscard]] std::size_t
reader_context::get_current_post_header_length(code_type code) const noexcept {
  assert(has_fde_processed());
  return get_post_header_length_for_code(post_header_lengths_, code);
}

} // namespace binsrv::event
