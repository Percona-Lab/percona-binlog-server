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
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include "binsrv/replication_mode_type.hpp"

#include "binsrv/event/code_type.hpp"
#include "binsrv/event/common_header_flag_type.hpp"
#include "binsrv/event/event.hpp"
#include "binsrv/event/protocol_traits.hpp"

#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

reader_context::reader_context(std::uint32_t encoded_server_version,
                               bool verify_checksum,
                               replication_mode_type replication_mode)
    : encoded_server_version_{encoded_server_version},
      verify_checksum_{verify_checksum}, replication_mode_{replication_mode},
      post_header_lengths_{
          get_hardcoded_post_header_lengths(encoded_server_version_)} {}

[[nodiscard]] std::size_t
reader_context::get_current_post_header_length(code_type code) const noexcept {
  return get_post_header_length_for_code(encoded_server_version_,
                                         post_header_lengths_, code);
}

void reader_context::process_event(const event &current_event) {
  bool processed{false};
  while (!processed) {
    switch (state_) {
    case state_type::rotate_artificial_expected:
      processed =
          process_event_in_rotate_artificial_expected_state(current_event);
      break;
    case state_type::format_description_expected:
      processed =
          process_event_in_format_description_expected_state(current_event);
      break;
    case state_type::previous_gtids_expected:
      processed = process_event_in_previous_gtids_expected_state(current_event);
      break;
    case state_type::gtid_log_expected:
      processed = process_event_in_gtid_log_expected_state(current_event);
      break;
    case state_type::any_other_expected:
      processed = process_event_in_any_other_expected_state(current_event);
      break;
    case state_type::rotate_or_stop_expected:
      processed = process_event_in_rotate_or_stop_expected_state(current_event);
      break;
    default:
      assert(false);
    }
  }
}

[[nodiscard]] bool
reader_context::process_event_in_rotate_artificial_expected_state(
    const event &current_event) {
  assert(state_ == state_type::rotate_artificial_expected);
  const auto &common_header{current_event.get_common_header()};

  // in the "rotate_artificial_expected" state we expect only artificial rotate
  // events
  const auto is_artificial{common_header.get_flags().has_element(
      common_header_flag_type::artificial)};
  const auto is_artificial_rotate{
      common_header.get_type_code() == code_type::rotate && is_artificial};
  if (!is_artificial_rotate) {
    util::exception_location().raise<std::logic_error>(
        "an artificial rotate event must be the very first event in the "
        "rotate_artificial_expected state");
  }

  // artificial rotate events must always have next event position and
  // timestamp set to 0
  if (common_header.get_timestamp_raw() != 0U) {
    util::exception_location().raise<std::logic_error>(
        "non-zero timestamp found in an artificial rotate event");
  }
  if (common_header.get_next_event_position_raw() != 0U) {
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

  info_only_event_ = true;
  // transition to the next state
  state_ = state_type::format_description_expected;
  return true;
}

[[nodiscard]] bool
reader_context::process_event_in_format_description_expected_state(
    const event &current_event) {
  assert(state_ == state_type::format_description_expected);
  const auto &common_header{current_event.get_common_header()};

  // in the "format_description_expected" state we expect only format
  // description events
  if (common_header.get_type_code() != code_type::format_description) {
    // TODO: this check should be performed just after the common header is
    //       parsed to make sure we rely on proper post_header lengths
    util::exception_location().raise<std::logic_error>(
        "format description event must follow an artificial rotate event");
  }

  const auto is_artificial{common_header.get_flags().has_element(
      common_header_flag_type::artificial)};
  if (is_artificial) {
    util::exception_location().raise<std::logic_error>(
        "format description event is not expected to be artificual");
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

  // check if server version in FDE is the same as the one extracted from
  // the connection object ('mysql_get_server_version()')
  if (post_header.get_encoded_server_version() != encoded_server_version_) {
    util::exception_location().raise<std::logic_error>(
        "unexpected server version in format description event");
  }
  // check if the values from the post_header_lengths array are the same as
  // generic_post_header_impl<code_type::xxx>::size_in_bytes for known events
  validate_post_header_lengths(
      encoded_server_version_, post_header.get_post_header_lengths_raw(),
      get_hardcoded_post_header_lengths(encoded_server_version_));

  post_header_lengths_ = post_header.get_post_header_lengths_raw();

  const auto &body{current_event.get_body<code_type::format_description>()};
  verify_checksum_ = body.has_checksum_algorithm();

  // here we differentiate format description events that were actually
  // written to the binlog files (as the very first events) and those
  // created by the server on the fly when client requested continuation
  // from a particular position (created only for the purpose of making
  // clients aware of the protocol specifics)

  // in position-based mode we have 2 cases:
  // 1) Starting from the beginning of a binary log file
  //    artificial ROTATE - should not be written
  //    FDE (common_header.next_event_position != 0) - should be written
  //    PREVIOUS_GTIDS_LOG - should be written
  //    ...
  // 2) Resuming from the middle of a binary log file
  //    artificial ROTATE - should not be written
  //    FDE (common_header.next_event_position == 0) - should not be written
  //    ...

  // in GTID-based mode we have 2 cases:
  // 1) Starting from the beginning of a binary log file
  //    artificial ROTATE - should not be written
  //    FDE (common_header.next_event_position != 0) - should be written
  //    PREVIOUS_GTIDS_LOG - should be written
  //    ...
  // 2) Resuming from the middle of a binary log file
  //    artificial ROTATE - should not be written
  //    FDE (common_header.next_event_position != 0) - should not be written
  //    PREVIOUS_GTIDS_LOG - should not be written
  //    ...

  // in other words, in GTID-based mode there is no way to distinguish whether
  // the FDE / PREVIOUS_GTIDS_LOG is pseudo and should not be written, or not -
  // that is why we rely only on externally supplied
  // "start_from_new_binlog_file" constructor's argument
  info_only_event_ = expect_ignorable_preamble_events_;
  if (replication_mode_ == replication_mode_type::position &&
      info_only_event_) {
    if (common_header.get_next_event_position_raw() != 0U) {
      util::exception_location().raise<std::logic_error>(
          "expected next event position set to zero in pseudo format "
          "description event");
    }
  }
  if (!info_only_event_) {
    validate_position_and_advance(common_header);
  }

  // transition to the next state:
  // the next expected event is PREVIOUS_GTIDS_LOG, unless we are in
  // position-based replication mode and this we resumed streaming from the
  // middle of a binlog file
  if (replication_mode_ == replication_mode_type::position &&
      info_only_event_) {
    state_ = state_type::gtid_log_expected;
    expect_ignorable_preamble_events_ = false;
  } else {
    state_ = state_type::previous_gtids_expected;
  }
  return true;
}

[[nodiscard]] bool
reader_context::process_event_in_previous_gtids_expected_state(
    const event &current_event) {
  assert(state_ == state_type::previous_gtids_expected);
  const auto &common_header{current_event.get_common_header()};

  // in the "previous_gtids_log_expected" state we expect only previous gtids
  // log events
  if (common_header.get_type_code() != code_type::previous_gtids_log) {
    util::exception_location().raise<std::logic_error>(
        "previous gtids log event must follow a format description event");
  }

  const auto is_artificial{common_header.get_flags().has_element(
      common_header_flag_type::artificial)};
  if (is_artificial) {
    util::exception_location().raise<std::logic_error>(
        "previous gtids log event is not expected to be artificial");
  }

  info_only_event_ = expect_ignorable_preamble_events_;
  expect_ignorable_preamble_events_ = false;
  if (!info_only_event_) {
    validate_position_and_advance(common_header);
  }

  state_ = state_type::gtid_log_expected;
  return true;
}

[[nodiscard]] bool reader_context::process_event_in_gtid_log_expected_state(
    const event &current_event) {
  assert(state_ == state_type::gtid_log_expected);
  const auto &common_header{current_event.get_common_header()};
  const auto code{common_header.get_type_code()};
  const auto is_artificial{common_header.get_flags().has_element(
      common_header_flag_type::artificial)};

  // early return here with "false" return code so that the while loop
  // in the main 'process_event()' method would repeat processing this
  // event but from the rotate_artificial_expected state (which is set here)
  if (code == code_type::rotate && is_artificial) {
    finish_transaction();
    reset_position();
    state_ = state_type::rotate_artificial_expected;
    return false;
  }

  // early return here, the exit from the
  // ((ANONYMOUS_GTID_LOG | GTID_LOG | GTID_TAGGED_LOG) <ANY>*)*
  // "star" loop
  if (code == code_type::rotate || code == code_type::stop) {
    finish_transaction();
    state_ = state_type::rotate_or_stop_expected;
    return false;
  }

  if (code != code_type::anonymous_gtid_log && code != code_type::gtid_log &&
      code != code_type::gtid_tagged_log) {
    // format description event must appear only once within a binlog
    util::exception_location().raise<std::logic_error>(
        "transaction group must start from one of the gtid log events");
  }

  if (replication_mode_ == replication_mode_type::gtid &&
      code == code_type::anonymous_gtid_log) {
    util::exception_location().raise<std::logic_error>(
        "unexpected anonymous gtid log event in gtid mode replication");
  }

  start_transaction(current_event);
  validate_position_and_advance(common_header);

  info_only_event_ = false;
  state_ = state_type::any_other_expected;
  return true;
}

[[nodiscard]] bool reader_context::process_event_in_any_other_expected_state(
    const event &current_event) {
  assert(state_ == state_type::any_other_expected);
  const auto &common_header{current_event.get_common_header()};
  const auto code{common_header.get_type_code()};
  const auto is_artificial{common_header.get_flags().has_element(
      common_header_flag_type::artificial)};

  // early return here, the exit from the
  // <ANY>*
  // "star" loop
  if (code == code_type::anonymous_gtid_log || code == code_type::gtid_log ||
      code == code_type::gtid_tagged_log) {
    finish_transaction();
    state_ = state_type::gtid_log_expected;
    return false;
  }

  // early return here with "false" return code so that the while loop
  // in the main 'process_event()' method would repeat processing this
  // event but from the rotate_artificial_expected state (which is set here)
  if (code == code_type::rotate && is_artificial) {
    finish_transaction();
    reset_position();
    state_ = state_type::rotate_artificial_expected;
    return false;
  }

  // early return here, the exit from the
  // ((ANONYMOUS_GTID_LOG | GTID_LOG | GTID_TAGGED_LOG) <ANY>*)*
  // "star" loop
  if (code == code_type::rotate || code == code_type::stop) {
    finish_transaction();
    state_ = state_type::rotate_or_stop_expected;
    return false;
  }

  if (code == code_type::format_description) {
    // format description event must appear only once within a binlog
    util::exception_location().raise<std::logic_error>(
        "encountered second format description event within the same binlog");
  }

  if (code == code_type::previous_gtids_log) {
    // previous_gtids_log event must appear only once within a binlog
    util::exception_location().raise<std::logic_error>(
        "encountered second previous gtids log event within the same binlog");
  }

  if (is_artificial) {
    util::exception_location().raise<std::logic_error>(
        "unexpected artificial event in the 'previous gtids log processed' "
        "state");
  }
  update_transaction(common_header);
  validate_position_and_advance(common_header);

  info_only_event_ = false;
  // not changing the state here - remain in 'any_other_expected'
  return true;
}

[[nodiscard]] bool
reader_context::process_event_in_rotate_or_stop_expected_state(
    const event &current_event) {
  assert(state_ == state_type::rotate_or_stop_expected);
  const auto &common_header{current_event.get_common_header()};
  const auto code{common_header.get_type_code()};

  // in the "rotate_or_stop_expected" state we expect only rotate
  // (non-artificial) or stop events
  if (code != code_type::rotate && code != code_type::stop) {
    util::exception_location().raise<std::logic_error>(
        "the very last event in the binlog must be either rotate or stop");
  }

  const auto is_artificial{common_header.get_flags().has_element(
      common_header_flag_type::artificial)};
  if (is_artificial) {
    util::exception_location().raise<std::logic_error>(
        "the very last event in the binlog is not expected to be artificial");
  }

  if (code == code_type::rotate) {
    // position in non-artificial rotate event post header must be equal to
    // magic_binlog_offset (4)
    if (current_event.get_post_header<code_type::rotate>().get_position_raw() !=
        magic_binlog_offset) {
      util::exception_location().raise<std::logic_error>(
          "unexpected position in an non-artificial rotate event post "
          "header");
    }
  }

  // the last event in binlog could be not only a non-artificial
  // ROTATE (in case when admin executed FLUSH BINARY LOGS) but STOP (when
  // the server was shut down)
  // in this latter case, we also reset the position in order to indicate
  // the end of the current cycle and expect new ROTATE(artificial) and
  // FORMAT_DESCRIPTION
  validate_position(common_header);
  reset_position();

  info_only_event_ = false;
  state_ = state_type::rotate_artificial_expected;

  return true;
}

void reader_context::validate_position(
    const common_header &common_header) const {
  if (common_header.get_next_event_position_raw() == 0U) {
    util::exception_location().raise<std::logic_error>(
        "next event position in the event common header cannot be zero");
  }

  // check if common_header.next_event_position matches current position
  // plus common_header.event_size
  if (position_ + common_header.get_event_size_raw() !=
      common_header.get_next_event_position_raw()) {
    util::exception_location().raise<std::logic_error>(
        "unexpected next event position in the event common header");
  }
}

void reader_context::validate_position_and_advance(
    const common_header &common_header) {
  validate_position(common_header);
  // simply advance current position
  position_ = common_header.get_next_event_position_raw();
}

void reader_context::reset_position() { position_ = 0U; }

void reader_context::start_transaction(const event &current_event) {
  switch (current_event.get_common_header().get_type_code()) {
  case code_type::anonymous_gtid_log: {
    // no need to update transaction_gtid_ as in anonymous gtid log event
    // the gtid part is expected to be empty
    if (!current_event.get_post_header<binsrv::event::code_type::gtid_log>()
             .get_gtid()
             .is_empty()) {
      util::exception_location().raise<std::logic_error>(
          "encountered non-empty gtid in the anonymous gtid log event");
    }
    const auto expected_transaction_length_raw{
        current_event.get_body<binsrv::event::code_type::anonymous_gtid_log>()
            .get_transaction_length_raw()};
    if (!std::in_range<std::uint32_t>(expected_transaction_length_raw)) {
      util::exception_location().raise<std::logic_error>(
          "transaction length in the anonymous gtid log event is too large");
    }
    expected_transaction_length_ =
        static_cast<std::uint32_t>(expected_transaction_length_raw);
  } break;
  case code_type::gtid_log: {
    transaction_gtid_ =
        current_event.get_post_header<binsrv::event::code_type::gtid_log>()
            .get_gtid();
    if (transaction_gtid_.is_empty()) {
      util::exception_location().raise<std::logic_error>(
          "encountered an empty gtid in the gtid log event");
    }
    const auto expected_transaction_length_raw{
        current_event.get_body<binsrv::event::code_type::gtid_log>()
            .get_transaction_length_raw()};
    if (!std::in_range<std::uint32_t>(expected_transaction_length_raw)) {
      util::exception_location().raise<std::logic_error>(
          "transaction length in the gtid log event is too large");
    }
    expected_transaction_length_ =
        static_cast<std::uint32_t>(expected_transaction_length_raw);
  } break;
  case code_type::gtid_tagged_log: {
    const auto &gtid_tagged_log_body{
        current_event.get_body<binsrv::event::code_type::gtid_tagged_log>()};
    transaction_gtid_ = gtid_tagged_log_body.get_gtid();
    if (transaction_gtid_.is_empty()) {
      util::exception_location().raise<std::logic_error>(
          "encountered an empty gtid in the gtid tagged log event");
    }
    const auto expected_transaction_length_raw{
        gtid_tagged_log_body.get_transaction_length_raw()};
    if (!std::in_range<std::uint32_t>(expected_transaction_length_raw)) {
      util::exception_location().raise<std::logic_error>(
          "transaction length in the gtid tagged log event is too large");
    }
    expected_transaction_length_ =
        static_cast<std::uint32_t>(expected_transaction_length_raw);
  } break;
  default:
    assert(false);
  }
  current_transaction_length_ =
      current_event.get_common_header().get_event_size_raw();
}

void reader_context::update_transaction(const common_header &common_header) {
  current_transaction_length_ += common_header.get_event_size_raw();
  if (current_transaction_length_ > expected_transaction_length_) {
    util::exception_location().raise<std::logic_error>(
        "current event exceeds declared transaction length");
  }
}

void reader_context::finish_transaction() {
  if (current_transaction_length_ != expected_transaction_length_) {
    util::exception_location().raise<std::logic_error>(
        "transaction did not end at the declared length");
  }
  transaction_gtid_ = gtids::gtid{};
  expected_transaction_length_ = 0U;
  current_transaction_length_ = 0U;
}

[[nodiscard]] const post_header_length_container &
reader_context::get_hardcoded_post_header_lengths(
    std::uint32_t encoded_server_version) noexcept {
  // here we use a trick with a templated lambda to initialize constexpr
  // arrays which would have expected post header lengths for all
  // event codes based on generic_post_header<xxx>::get_size_in_bytes()

  // we ignore the very first element in the code_type enum
  // (code_type::unknown) since the post header length for this value is
  // simply not included into FDE post header

  using size_in_bytes_helper =
      decltype([]<std::size_t Index>(
                   std::uint32_t version) constexpr -> std::size_t {
        constexpr auto code{util::index_to_enum<code_type>(Index + 1U)};
        if constexpr (requires(std::uint32_t test_version) {
                        {
                          generic_post_header<code>::get_size_in_bytes(
                              test_version)
                        } -> std::same_as<std::size_t>;
                      }) {
          return generic_post_header<code>::get_size_in_bytes(version);
        } else {
          return generic_post_header<code>::size_in_bytes;
        }
      });

  constexpr auto hardcoded_post_header_lengths_generator{
      []<std::size_t... IndexPack>(std::uint32_t version,
                                   std::index_sequence<IndexPack...>) constexpr
          -> post_header_length_container {
        return {
          static_cast<encoded_post_header_length_type>(
              size_in_bytes_helper{}.template operator()<IndexPack>(version))...
        };
      }};

  // the remainder of the 'earliest_supported_post_header_lengths' container
  // will be default-initialized
  static constexpr post_header_length_container
      earliest_supported_post_header_lengths{
          hardcoded_post_header_lengths_generator(
              earliest_supported_protocol_server_version,
              std::make_index_sequence<
                  get_number_of_events(
                      earliest_supported_protocol_server_version) -
                  1U>{})};
  static constexpr post_header_length_container
      latest_known_post_header_lengths{hardcoded_post_header_lengths_generator(
          latest_known_protocol_server_version,
          std::make_index_sequence<get_number_of_events(
                                       latest_known_protocol_server_version) -
                                   1U>{})};

  return encoded_server_version < latest_known_protocol_server_version
             ? earliest_supported_post_header_lengths
             : latest_known_post_header_lengths;
}

} // namespace binsrv::event
