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

#include "binsrv/events/reader_context.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "binsrv/replication_mode_type.hpp"

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_header_flag_type.hpp"
#include "binsrv/events/event.hpp"
#include "binsrv/events/event_view.hpp"
#include "binsrv/events/protocol_traits.hpp"

#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::events {

reader_context::reader_context(std::uint32_t encoded_server_version,
                               bool checksum_verification_enabled,
                               replication_mode_type replication_mode,
                               std::string_view binlog_name,
                               std::uint32_t position)
    : encoded_server_version_{encoded_server_version},
      checksum_verification_enabled_{checksum_verification_enabled},
      footer_expected_{checksum_verification_enabled},
      replication_mode_{replication_mode}, binlog_name_{binlog_name},
      position_{position == 0U ? static_cast<std::uint32_t>(magic_binlog_offset)
                               : position},
      post_header_lengths_{
          get_known_post_header_lengths(encoded_server_version_)} {}

[[nodiscard]] std::size_t
reader_context::get_current_post_header_length(code_type code) const noexcept {
  return get_post_header_length_for_code(encoded_server_version_,
                                         post_header_lengths_, code);
}

[[nodiscard]] const post_header_length_container &
reader_context::get_known_post_header_lengths(
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

  constexpr auto known_post_header_lengths_generator{
      []<std::size_t... IndexPack>(std::uint32_t version,
                                   std::index_sequence<IndexPack...>) constexpr
          -> post_header_length_container {
        return {
          static_cast<encoded_post_header_length_type>(
              size_in_bytes_helper{}.template operator()<IndexPack>(version))...
        };
      }};

  // the remainder of the 'earliest' container (the gtid_tagged_log element)
  // will be default-initialized
  static constexpr post_header_length_container earliest{
      known_post_header_lengths_generator(
          earliest_supported_protocol_server_version,
          std::make_index_sequence<
              get_number_of_events(earliest_supported_protocol_server_version) -
              1U>{})};
  static constexpr post_header_length_container latest{
      known_post_header_lengths_generator(
          latest_known_protocol_server_version,
          std::make_index_sequence<get_number_of_events(
                                       latest_known_protocol_server_version) -
                                   1U>{})};

  return encoded_server_version < latest_known_protocol_server_version
             ? earliest
             : latest;
}

[[nodiscard]] const post_header_length_container &
reader_context::get_hardcoded_post_header_lengths(
    std::uint32_t encoded_server_version) noexcept {
  // clang-format off
  // https://github.com/mysql/mysql-server/blob/mysql-8.0.45/libbinlogevents/src/control_events.cpp#L96
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.8/libs/mysql/binlog/event/control_events.cpp#L104
  // https://github.com/mysql/mysql-server/blob/mysql-9.6.0/libs/mysql/binlog/event/control_events.cpp#L104

  // here is a formatted version of 8.0 post header length array definition
  // from the MySQL Server source code

  /*
  static uint8_t server_event_header_length[] = {
    0,
    QUERY_HEADER_LEN,
    STOP_HEADER_LEN,
    ROTATE_HEADER_LEN,
    INTVAR_HEADER_LEN,
    0,
    // Unused because the code for Slave log event was removed.
    // (15th Oct. 2010)
    0,
    0,
    APPEND_BLOCK_HEADER_LEN,
    0,
    DELETE_FILE_HEADER_LEN,
    0,
    RAND_HEADER_LEN,
    USER_VAR_HEADER_LEN,
    FORMAT_DESCRIPTION_HEADER_LEN,
    XID_HEADER_LEN,
    BEGIN_LOAD_QUERY_HEADER_LEN,
    EXECUTE_LOAD_QUERY_HEADER_LEN,
    TABLE_MAP_HEADER_LEN,
    0,
    0,
    0,
    ROWS_HEADER_LEN_V1, // WRITE_ROWS_EVENT_V1
    ROWS_HEADER_LEN_V1, // UPDATE_ROWS_EVENT_V1
    ROWS_HEADER_LEN_V1, // DELETE_ROWS_EVENT_V1
    INCIDENT_HEADER_LEN,
    0, // HEARTBEAT_LOG_EVENT
    IGNORABLE_HEADER_LEN,
    IGNORABLE_HEADER_LEN,
    ROWS_HEADER_LEN_V2,
    ROWS_HEADER_LEN_V2,
    ROWS_HEADER_LEN_V2,
    Gtid_event::POST_HEADER_LENGTH, // GTID_EVENT
    Gtid_event::POST_HEADER_LENGTH, // ANONYMOUS_GTID_EVENT
    IGNORABLE_HEADER_LEN,
    TRANSACTION_CONTEXT_HEADER_LEN,
    VIEW_CHANGE_HEADER_LEN,
    XA_PREPARE_HEADER_LEN,
    ROWS_HEADER_LEN_V2,
    TRANSACTION_PAYLOAD_EVENT,
    0, // HEARTBEAT_LOG_EVENT_V2
  };
  */

  // here is a formatted version of 8.4 post header length array definition
  // from the MySQL Server source code
  /*
  static uint8_t server_event_header_length[] = {
    0,
    QUERY_HEADER_LEN,
    STOP_HEADER_LEN,
    ROTATE_HEADER_LEN,
    INTVAR_HEADER_LEN,
    0,
    // Unused because the code for Slave log event was removed.
    // (15th Oct. 2010)
    0,
    0,
    APPEND_BLOCK_HEADER_LEN,
    0,
    DELETE_FILE_HEADER_LEN,
    0,
    RAND_HEADER_LEN,
    USER_VAR_HEADER_LEN,
    FORMAT_DESCRIPTION_HEADER_LEN,
    XID_HEADER_LEN,
    BEGIN_LOAD_QUERY_HEADER_LEN,
    EXECUTE_LOAD_QUERY_HEADER_LEN,
    TABLE_MAP_HEADER_LEN,
    0,
    0,
    0,
    // First three values are unused as the code for V1 Rows events were
    // removed in 8.4.0
    0,
    0,
    0,
    INCIDENT_HEADER_LEN,
    0, // HEARTBEAT_LOG_EVENT
    IGNORABLE_HEADER_LEN,
    IGNORABLE_HEADER_LEN,
    ROWS_HEADER_LEN_V2,
    ROWS_HEADER_LEN_V2,
    ROWS_HEADER_LEN_V2,
    Gtid_event::POST_HEADER_LENGTH, // GTID_EVENT
    Gtid_event::POST_HEADER_LENGTH, // ANONYMOUS_GTID_EVENT
    IGNORABLE_HEADER_LEN,
    TRANSACTION_CONTEXT_HEADER_LEN,
    VIEW_CHANGE_HEADER_LEN,
    XA_PREPARE_HEADER_LEN,
    ROWS_HEADER_LEN_V2,
    TRANSACTION_PAYLOAD_EVENT,
    0, // HEARTBEAT_LOG_EVENT_V2
    0, // GTID_TAGGED_LOG_EVENT
  };
  */

  // in addition, here are dumps of the FORMAT_DESCRIPTION event generated
  // by this utility

  // for the 8.0 MySQL Server
  /*
  start_v3            => 0
  query               => 13
  stop                => 0
  rotate              => 8
  intvar              => 0
  obsolete_6          => 0
  slave               => 0
  obsolete_8          => 0
  append_block        => 4
  obsolete_10         => 0
  delete_file         => 4
  obsolete_12         => 0
  rand                => 0
  user_var            => 0
  format_description  => 98
  xid                 => 0
  begin_load_query    => 4
  execute_load_query  => 26
  table_map           => 8
  obsolete_20         => 0
  obsolete_21         => 0
  obsolete_22         => 0
  write_rows_v1       => 8
  update_rows_v1      => 8
  delete_rows_v1      => 8
  incident            => 2
  heartbeat_log       => 0
  ignorable_log       => 0
  rows_query_log      => 0
  write_rows          => 10
  update_rows         => 10
  delete_rows         => 10
  gtid_log            => 42
  anonymous_gtid_log  => 42
  previous_gtids_log  => 0
  transaction_context => 18
  view_change         => 52
  xa_prepare_log      => 0
  partial_update_rows => 10
  transaction_payload => 40
  heartbeat_log_v2    => 0
  */

  // for the 8.4 MySQL Server
  /*
  start_v3            => 0
  query               => 13
  stop                => 0
  rotate              => 8
  intvar              => 0
  obsolete_6          => 0
  slave               => 0
  obsolete_8          => 0
  append_block        => 4
  obsolete_10         => 0
  delete_file         => 4
  obsolete_12         => 0
  rand                => 0
  user_var            => 0
  format_description  => 99
  xid                 => 0
  begin_load_query    => 4
  execute_load_query  => 26
  table_map           => 8
  obsolete_20         => 0
  obsolete_21         => 0
  obsolete_22         => 0
  write_rows_v1       => 0
  update_rows_v1      => 0
  delete_rows_v1      => 0
  incident            => 2
  heartbeat_log       => 0
  ignorable_log       => 0
  rows_query_log      => 0
  write_rows          => 10
  update_rows         => 10
  delete_rows         => 10
  gtid_log            => 42
  anonymous_gtid_log  => 42
  previous_gtids_log  => 0
  transaction_context => 18
  view_change         => 52
  xa_prepare_log      => 0
  partial_update_rows => 10
  transaction_payload => 40
  heartbeat_log_v2    => 0
  gtid_tagged_log     => 0
  */

  // 8.0 - 8.4 comparison table
  /*
  +-----------+---------------------+-----------+-----------+----------+
  |   Code    |      Mnemonic       |    8.0    |    8.4    | Mismatch |
  +-----------+---------------------+-----------+-----------+----------+
  |         1 | start_v3            |         0 |         0 |          |
  |         2 | query               |        13 |        13 |          |
  |         3 | stop                |         0 |         0 |          |
  |         4 | rotate              |         8 |         8 |          |
  |         5 | intvar              |         0 |         0 |          |
  |         6 | obsolete_6          |         0 |         0 |          |
  |         7 | slave               |         0 |         0 |          |
  |         8 | obsolete_8          |         0 |         0 |          |
  |         9 | append_block        |         4 |         4 |          |
  |        10 | obsolete_10         |         0 |         0 |          |
  |        11 | delete_file         |         4 |         4 |          |
  |        12 | obsolete_12         |         0 |         0 |          |
  |        13 | rand                |         0 |         0 |          |
  |        14 | user_var            |         0 |         0 |          |
  |        15 | format_description  |        98 |        99 |        * |
  |        16 | xid                 |         0 |         0 |          |
  |        17 | begin_load_query    |         4 |         4 |          |
  |        18 | execute_load_query  |        26 |        26 |          |
  |        19 | table_map           |         8 |         8 |          |
  |        20 | obsolete_20         |         0 |         0 |          |
  |        21 | obsolete_21         |         0 |         0 |          |
  |        22 | obsolete_22         |         0 |         0 |          |
  |        23 | write_rows_v1       |         8 |         0 |        * |
  |        24 | update_rows_v1      |         8 |         0 |        * |
  |        25 | delete_rows_v1      |         8 |         0 |        * |
  |        26 | incident            |         2 |         2 |          |
  |        27 | heartbeat_log       |         0 |         0 |          |
  |        28 | ignorable_log       |         0 |         0 |          |
  |        29 | rows_query_log      |         0 |         0 |          |
  |        30 | write_rows          |        10 |        10 |          |
  |        31 | update_rows         |        10 |        10 |          |
  |        32 | delete_rows         |        10 |        10 |          |
  |        33 | gtid_log            |        42 |        42 |          |
  |        34 | anonymous_gtid_log  |        42 |        42 |          |
  |        35 | previous_gtids_log  |         0 |         0 |          |
  |        36 | transaction_context |        18 |        18 |          |
  |        37 | view_change         |        52 |        52 |          |
  |        38 | xa_prepare_log      |         0 |         0 |          |
  |        39 | partial_update_rows |        10 |        10 |          |
  |        40 | transaction_payload |        40 |        40 |          |
  |        41 | heartbeat_log_v2    |         0 |         0 |          |
  |        42 | gtid_tagged_log     |           |         0 |        * |
  +-----------+---------------------+-----------+-----------+----------+
  */
  // clang-format on

  // to sum up, 8.4 in comparison to 8.0 has:
  // - one more event GTID_TAGGED_LOG_EVENT (code 42)
  // - because of this the length of the FORMAT_DESCRIPTION_EVENT post-header
  //   (code 15) also increased by 1 byte
  // - also WRITE_ROWS_V1 (code 23), UPDATE_ROWS_V1 (code 24), and
  //   DELETE_ROWS_V1 (code 25) are lo longer supported and their post-header
  //   lengths changed from 8 bytes to 0

  // the remainder of the 'earliest' container (the gtid_tagged_log element)
  // will be default-initialized
  static constexpr post_header_length_container earliest{
      0,  // start_v3
      13, // query
      0,  // stop
      8,  // rotate
      0,  // intvar
      0,  // obsolete_6
      0,  // slave
      0,  // obsolete_8
      4,  // append_block
      0,  // obsolete_10
      4,  // delete_file
      0,  // obsolete_12
      0,  // rand
      0,  // user_var
      98, // format_description
      0,  // xid
      4,  // begin_load_query
      26, // execute_load_query
      8,  // table_map
      0,  // obsolete_20
      0,  // obsolete_21
      0,  // obsolete_22
      8,  // write_rows_v1
      8,  // update_rows_v1
      8,  // delete_rows_v1
      2,  // incident
      0,  // heartbeat_log
      0,  // ignorable_log
      0,  // rows_query_log
      10, // write_rows
      10, // update_rows
      10, // delete_rows
      42, // gtid_log
      42, // anonymous_gtid_log
      0,  // previous_gtids_log
      18, // transaction_context
      52, // view_change
      0,  // xa_prepare_log
      10, // partial_update_rows
      40, // transaction_payload
      0   // heartbeat_log_v2
  };
  static constexpr post_header_length_container latest{
      0,  // start_v3
      13, // query
      0,  // stop
      8,  // rotate
      0,  // intvar
      0,  // obsolete_6
      0,  // slave
      0,  // obsolete_8
      4,  // append_block
      0,  // obsolete_10
      4,  // delete_file
      0,  // obsolete_12
      0,  // rand
      0,  // user_var
      99, // format_description
      0,  // xid
      4,  // begin_load_query
      26, // execute_load_query
      8,  // table_map
      0,  // obsolete_20
      0,  // obsolete_21
      0,  // obsolete_22
      0,  // write_rows_v1
      0,  // update_rows_v1
      0,  // delete_rows_v1
      2,  // incident
      0,  // heartbeat_log
      0,  // ignorable_log
      0,  // rows_query_log
      10, // write_rows
      10, // update_rows
      10, // delete_rows
      42, // gtid_log
      42, // anonymous_gtid_log
      0,  // previous_gtids_log
      18, // transaction_context
      52, // view_change
      0,  // xa_prepare_log
      10, // partial_update_rows
      40, // transaction_payload
      0,  // heartbeat_log_v2
      0   // gtid_tagged_log
  };

  return encoded_server_version < latest_known_protocol_server_version
             ? earliest
             : latest;
}

[[nodiscard]] bool
reader_context::process_event_view(const event_view &current_event_v) {
  bool info_only{false};
  bool processed{false};
  while (!processed) {
    switch (state_) {
    case state_type::rotate_artificial_expected:
      processed = process_event_in_rotate_artificial_expected_state(
          current_event_v, info_only);
      break;
    case state_type::format_description_expected:
      processed = process_event_in_format_description_expected_state(
          current_event_v, info_only);
      break;
    case state_type::previous_gtids_expected:
      processed = process_event_in_previous_gtids_expected_state(
          current_event_v, info_only);
      break;
    case state_type::gtid_log_expected:
      processed =
          process_event_in_gtid_log_expected_state(current_event_v, info_only);
      break;
    case state_type::any_other_expected:
      processed =
          process_event_in_any_other_expected_state(current_event_v, info_only);
      break;
    case state_type::rotate_or_stop_expected:
      processed = process_event_in_rotate_or_stop_expected_state(
          current_event_v, info_only);
      break;
    default:
      assert(false);
    }
  }
  return info_only;
}

[[nodiscard]] bool
reader_context::process_event_in_rotate_artificial_expected_state(
    const event_view &current_event_v, bool &info_only) {
  assert(state_ == state_type::rotate_artificial_expected);
  const auto common_header_v{current_event_v.get_common_header_view()};

  // in the "rotate_artificial_expected" state we expect only artificial rotate
  // events
  const auto is_artificial{common_header_v.get_flags().has_element(
      common_header_flag_type::artificial)};
  const auto is_artificial_rotate{
      common_header_v.get_type_code() == code_type::rotate && is_artificial};
  if (!is_artificial_rotate) {
    util::exception_location().raise<std::logic_error>(
        "an artificial rotate event must be the very first event in the "
        "rotate_artificial_expected state");
  }

  // artificial rotate events must always have next event position and
  // timestamp set to 0
  if (common_header_v.get_timestamp_raw() != 0U) {
    util::exception_location().raise<std::logic_error>(
        "non-zero timestamp found in an artificial rotate event");
  }
  if (common_header_v.get_next_event_position_raw() != 0U) {
    util::exception_location().raise<std::logic_error>(
        "non-zero next event position found in an artificial rotate event");
  }

  const auto current_post_header{generic_post_header<code_type::rotate>{
      current_event_v.get_post_header_raw()}};
  const auto current_body{
      generic_body<code_type::rotate>{current_event_v.get_body_raw()}};
  if (replication_mode_ == replication_mode_type::position) {
    if (current_body.get_readable_binlog() == binlog_name_) {
      // in position-based replication mode, when we continue streaming to the
      // same binlog file, we expect the artificial rotate event to have the
      // same position as the one supplied to the constructor
      if (current_post_header.get_position_raw() != position_) {
        util::exception_location().raise<std::logic_error>(
            "unexpected position found in an artificial rotate event in "
            "position-based replication mode");
      }
    } else {
      // in position-based replication mode, when we start streaming to a new
      // binlog file, we expect the artificial rotate event
      // to have the position equal to "magic_offset" (4)
      if (current_post_header.get_position_raw() != magic_binlog_offset) {
        util::exception_location().raise<std::logic_error>(
            "unexpected position found in an artificial rotate event in "
            "position-based replication mode");
      }
    }
  } else {
    // in GTID-based replication mode we expect the artificial rotate event to
    // always have position set to "magic_offset" (4)
    if (current_post_header.get_position_raw() != magic_binlog_offset) {
      util::exception_location().raise<std::logic_error>(
          "unexpected position found in an artificial rotate event in "
          "GTID-based replication mode");
    }
  }
  // whether we should expect info-only FORMAT_DESCRIPTION and optional
  // PREVIOUS_GTIDS_LOG events depends on whether we resumed streaming to an
  // existing non-empty binlog file or not
  expect_info_only_preamble_events_ =
      (cycle_number_ == 0U) &&
      (current_body.get_readable_binlog() == binlog_name_) &&
      (position_ > magic_binlog_offset);

  if (current_body.get_readable_binlog() != binlog_name_) {
    // in the case when binlog name in the artificial rotate event does not
    // match the one specified in the last saved one, we should update it here
    // and reset the position to "magic_offset" (4)

    // please notice that the very first time the reader_context is created
    // 'binlog_name' is initialized with the 'storage.get_current_binlog_name()'
    // value

    // also, when the storage objects is created on an empty directory,
    // 'storage.get_current_binlog_name()' returns an empty string which will
    // never be equal to any real binlog name
    binlog_name_ = current_body.get_readable_binlog();
    reset_position();
  }

  ++cycle_number_;
  info_only = true;
  // transition to the next state
  state_ = state_type::format_description_expected;
  return true;
}

[[nodiscard]] bool
reader_context::process_event_in_format_description_expected_state(
    const event_view &current_event_v, bool &info_only) {
  assert(state_ == state_type::format_description_expected);
  const auto common_header_v{current_event_v.get_common_header_view()};

  // in the "format_description_expected" state we expect only format
  // description events
  if (common_header_v.get_type_code() != code_type::format_description) {
    // TODO: this check should be performed just after the common header is
    //       parsed to make sure we rely on proper post_header lengths
    util::exception_location().raise<std::logic_error>(
        "format description event must follow an artificial rotate event");
  }

  const auto is_artificial{common_header_v.get_flags().has_element(
      common_header_flag_type::artificial)};
  if (is_artificial) {
    util::exception_location().raise<std::logic_error>(
        "format description event is not expected to be artificial");
  }

  const auto post_header{generic_post_header<code_type::format_description>{
      get_current_encoded_server_version(),
      current_event_v.get_post_header_raw()}};

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
      get_known_post_header_lengths(encoded_server_version_));

  post_header_lengths_ = post_header.get_post_header_lengths_raw();

  const auto body{generic_body<code_type::format_description>{
      current_event_v.get_body_raw()}};
  footer_expected_ = body.has_checksum_algorithm();

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
  // the FDE / PREVIOUS_GTIDS_LOG should be written to the binlog file or
  // not - that is why we rely only on the value of the
  // "expect_info_only_preamble_events" calculated previously
  info_only = expect_info_only_preamble_events_;
  if (replication_mode_ == replication_mode_type::position && info_only) {
    if (common_header_v.get_next_event_position_raw() != 0U) {
      util::exception_location().raise<std::logic_error>(
          "expected next event position set to zero in pseudo format "
          "description event");
    }
  }
  if (!info_only) {
    validate_position_and_advance(common_header_v);
  }

  // the next expected event is PREVIOUS_GTIDS_LOG, unless we are in
  // position-based replication mode and we resumed streaming to an
  // existing non-empty binlog file, in which case we expect the next event
  // to be one of the GTID_LOG events
  state_ = (replication_mode_ == replication_mode_type::position && info_only)
               ? state_type::gtid_log_expected
               : state_type::previous_gtids_expected;
  return true;
}

[[nodiscard]] bool
reader_context::process_event_in_previous_gtids_expected_state(
    const event_view &current_event_v, bool &info_only) {
  assert(state_ == state_type::previous_gtids_expected);
  const auto common_header_v{current_event_v.get_common_header_view()};

  // in the "previous_gtids_log_expected" state we expect only previous gtids
  // log events
  if (common_header_v.get_type_code() != code_type::previous_gtids_log) {
    util::exception_location().raise<std::logic_error>(
        "previous gtids log event must follow a format description event");
  }

  const auto is_artificial{common_header_v.get_flags().has_element(
      common_header_flag_type::artificial)};
  if (is_artificial) {
    util::exception_location().raise<std::logic_error>(
        "previous gtids log event is not expected to be artificial");
  }

  info_only = expect_info_only_preamble_events_;
  if (!info_only) {
    validate_position_and_advance(common_header_v);
  }

  state_ = state_type::gtid_log_expected;
  return true;
}

[[nodiscard]] bool reader_context::process_event_in_gtid_log_expected_state(
    const event_view &current_event_v, bool &info_only) {
  assert(state_ == state_type::gtid_log_expected);
  const auto common_header_v{current_event_v.get_common_header_view()};
  const auto code{common_header_v.get_type_code()};
  const auto is_artificial{common_header_v.get_flags().has_element(
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

  start_transaction(current_event_v);
  validate_position_and_advance(common_header_v);

  info_only = false;
  state_ = state_type::any_other_expected;
  return true;
}

[[nodiscard]] bool reader_context::process_event_in_any_other_expected_state(
    const event_view &current_event_v, bool &info_only) {
  assert(state_ == state_type::any_other_expected);
  const auto common_header_v{current_event_v.get_common_header_view()};
  const auto code{common_header_v.get_type_code()};
  const auto is_artificial{common_header_v.get_flags().has_element(
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
  update_transaction(common_header_v);
  validate_position_and_advance(common_header_v);

  info_only = false;
  // not changing the state here - remain in 'any_other_expected'
  return true;
}

[[nodiscard]] bool
reader_context::process_event_in_rotate_or_stop_expected_state(
    const event_view &current_event_v, bool &info_only) {
  assert(state_ == state_type::rotate_or_stop_expected);
  const auto common_header_v{current_event_v.get_common_header_view()};
  const auto code{common_header_v.get_type_code()};

  // in the "rotate_or_stop_expected" state we expect only rotate
  // (non-artificial) or stop events
  if (code != code_type::rotate && code != code_type::stop) {
    util::exception_location().raise<std::logic_error>(
        "the very last event in the binlog must be either rotate or stop");
  }

  const auto is_artificial{common_header_v.get_flags().has_element(
      common_header_flag_type::artificial)};
  if (is_artificial) {
    util::exception_location().raise<std::logic_error>(
        "the very last event in the binlog is not expected to be artificial");
  }

  if (code == code_type::rotate) {
    // position in non-artificial rotate event post header must be equal to
    // magic_binlog_offset (4)
    const auto current_post_header{generic_post_header<code_type::rotate>{
        current_event_v.get_post_header_raw()}};
    if (current_post_header.get_position_raw() != magic_binlog_offset) {
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
  validate_position(common_header_v);
  reset_position();

  info_only = false;
  state_ = state_type::rotate_artificial_expected;

  return true;
}

void reader_context::validate_position(
    const common_header_view &common_header_v) const {
  if (common_header_v.get_next_event_position_raw() == 0U) {
    util::exception_location().raise<std::logic_error>(
        "next event position in the event common header cannot be zero");
  }

  // check if common_header.next_event_position matches current position
  // plus common_header.event_size
  if (position_ + common_header_v.get_event_size_raw() !=
      common_header_v.get_next_event_position_raw()) {
    util::exception_location().raise<std::logic_error>(
        "unexpected next event position in the event common header");
  }
}

void reader_context::validate_position_and_advance(
    const common_header_view &common_header_v) {
  validate_position(common_header_v);
  // simply advance current position
  position_ = common_header_v.get_next_event_position_raw();
}

void reader_context::reset_position() { position_ = magic_binlog_offset; }

void reader_context::start_transaction(const event_view &current_event_v) {
  const common_header_view common_header_v{
      current_event_v.get_common_header_view()};
  switch (common_header_v.get_type_code()) {
  case code_type::anonymous_gtid_log: {
    // no need to update transaction_gtid_ as in anonymous gtid log event
    // the gtid part is expected to be empty
    const auto current_post_header{
        generic_post_header<code_type::anonymous_gtid_log>{
            current_event_v.get_post_header_raw()}};
    if (!current_post_header.get_gtid().is_empty()) {
      util::exception_location().raise<std::logic_error>(
          "encountered non-empty gtid in the anonymous gtid log event");
    }
    const auto current_body{generic_body<code_type::anonymous_gtid_log>{
        current_event_v.get_body_raw()}};
    const auto expected_transaction_length_raw{
        current_body.get_transaction_length_raw()};
    if (!std::in_range<std::uint32_t>(expected_transaction_length_raw)) {
      util::exception_location().raise<std::logic_error>(
          "transaction length in the anonymous gtid log event is too large");
    }
    expected_transaction_length_ =
        static_cast<std::uint32_t>(expected_transaction_length_raw);
  } break;
  case code_type::gtid_log: {
    const auto current_post_header{generic_post_header<code_type::gtid_log>{
        current_event_v.get_post_header_raw()}};
    transaction_gtid_ = current_post_header.get_gtid();
    if (transaction_gtid_.is_empty()) {
      util::exception_location().raise<std::logic_error>(
          "encountered an empty gtid in the gtid log event");
    }
    const auto current_body{
        generic_body<code_type::gtid_log>{current_event_v.get_body_raw()}};
    const auto expected_transaction_length_raw{
        current_body.get_transaction_length_raw()};
    if (!std::in_range<std::uint32_t>(expected_transaction_length_raw)) {
      util::exception_location().raise<std::logic_error>(
          "transaction length in the gtid log event is too large");
    }
    expected_transaction_length_ =
        static_cast<std::uint32_t>(expected_transaction_length_raw);
  } break;
  case code_type::gtid_tagged_log: {
    const auto current_body{generic_body<code_type::gtid_tagged_log>{
        current_event_v.get_body_raw()}};
    transaction_gtid_ = current_body.get_gtid();
    if (transaction_gtid_.is_empty()) {
      util::exception_location().raise<std::logic_error>(
          "encountered an empty gtid in the gtid tagged log event");
    }
    const auto expected_transaction_length_raw{
        current_body.get_transaction_length_raw()};
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
  current_transaction_length_ = common_header_v.get_event_size_raw();
}

void reader_context::update_transaction(
    const common_header_view &common_header_v) {
  current_transaction_length_ += common_header_v.get_event_size_raw();
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

} // namespace binsrv::events
