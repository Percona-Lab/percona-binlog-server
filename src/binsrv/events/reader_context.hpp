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

#ifndef BINSRV_EVENTS_READER_CONTEXT_HPP
#define BINSRV_EVENTS_READER_CONTEXT_HPP

#include "binsrv/events/reader_context_fwd.hpp" // IWYU pragma: export

#include <cstdint>

#include "binsrv/replication_mode_type_fwd.hpp"

#include "binsrv/gtids/gtid.hpp"

#include "binsrv/events/common_header_view_fwd.hpp"
#include "binsrv/events/event_fwd.hpp"
#include "binsrv/events/event_view_fwd.hpp"
#include "binsrv/events/protocol_traits.hpp"

namespace binsrv::events {

class [[nodiscard]] reader_context {
public:
  reader_context(std::uint32_t connection_encoded_server_version,
                 bool checksum_verification_enabled,
                 replication_mode_type replication_mode,
                 std::string_view binlog_name, std::uint32_t position);

  [[nodiscard]] bool is_fresh() const noexcept { return cycle_number_ == 0U; }
  [[nodiscard]] std::uint32_t
  get_connection_encoded_server_version() const noexcept {
    return connection_encoded_server_version_;
  }
  [[nodiscard]] std::uint32_t
  get_current_encoded_server_version() const noexcept {
    return current_encoded_server_version_;
  }
  [[nodiscard]] bool is_checksum_verification_enabled() const noexcept {
    return checksum_verification_enabled_;
  }
  [[nodiscard]] bool is_footer_expected() const noexcept {
    return footer_expected_;
  }

  [[nodiscard]] std::size_t
  get_current_post_header_length(code_type code) const noexcept;

  [[nodiscard]] bool has_transaction_gtid() const noexcept {
    return !transaction_gtid_.is_empty();
  }
  [[nodiscard]] const gtids::gtid &get_transaction_gtid() const noexcept {
    return transaction_gtid_;
  }
  [[nodiscard]] bool is_at_transaction_boundary() const noexcept {
    return (state_ == state_type::any_other_expected &&
            current_transaction_length_ == expected_transaction_length_) ||
           (state_ == state_type::rotate_artificial_expected);
  }

  [[nodiscard]] static const post_header_length_container &
  get_known_post_header_lengths(std::uint32_t encoded_server_version) noexcept;

  [[nodiscard]] static const post_header_length_container &
  get_hardcoded_post_header_lengths(
      std::uint32_t encoded_server_version) noexcept;

  [[nodiscard]] bool process_event_view(const event_view &current_event_v);

private:
  // this class implements the logic of the following state machine
  // (
  //   ROTATE(artificial)
  //   FORMAT_DESCRIPTION
  //   PREVIOUS_GTIDS_LOG?
  //   ((ANONYMOUS_GTID_LOG | GTID_LOG | GTID_TAGGED_LOG) <ANY>*)*
  //   (ROTATE | STOP)?
  // )+
  enum class state_type : std::uint8_t {
    rotate_artificial_expected,
    format_description_expected,
    previous_gtids_expected,
    gtid_log_expected,
    any_other_expected,
    rotate_or_stop_expected
  };
  state_type state_{state_type::rotate_artificial_expected};
  std::uint32_t connection_encoded_server_version_;
  std::uint32_t current_encoded_server_version_;

  // indicates whether user requested checksum verification
  // (in the configuration file)
  bool checksum_verification_enabled_;

  // indicates whether we should expect a footer with a checksum in the
  // upcoming event
  bool footer_expected_;

  replication_mode_type replication_mode_;
  std::string binlog_name_;
  std::uint32_t position_;
  post_header_length_container post_header_lengths_{};

  gtids::gtid transaction_gtid_{};
  std::uint32_t expected_transaction_length_{0U};
  std::uint32_t current_transaction_length_{0U};

  bool expect_info_only_preamble_events_{false};

  std::size_t cycle_number_{0U};

  [[nodiscard]] bool process_event_in_rotate_artificial_expected_state(
      const event_view &current_event_v, bool &info_only);
  [[nodiscard]] bool process_event_in_format_description_expected_state(
      const event_view &current_event_v, bool &info_only);
  [[nodiscard]] bool process_event_in_previous_gtids_expected_state(
      const event_view &current_event_v, bool &info_only);
  [[nodiscard]] bool
  process_event_in_gtid_log_expected_state(const event_view &current_event_v,
                                           bool &info_only);
  [[nodiscard]] bool
  process_event_in_any_other_expected_state(const event_view &current_event_v,
                                            bool &info_only);
  [[nodiscard]] bool process_event_in_rotate_or_stop_expected_state(
      const event_view &current_event_v, bool &info_only);

  void validate_position(const common_header_view &common_header_v) const;
  void validate_position_and_advance(const common_header_view &common_header_v);
  void reset_position();

  void start_transaction(const event_view &current_event_v);
  void update_transaction(const common_header_view &common_header_v);
  void finish_transaction();
};

} // namespace binsrv::events

#endif // BINSRV_EVENTS_READER_CONTEXT_HPP
