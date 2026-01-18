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

#include <cstdint>

#include "binsrv/replication_mode_type_fwd.hpp"

#include "binsrv/gtids/gtid.hpp"

#include "binsrv/event/common_header_fwd.hpp"
#include "binsrv/event/event_fwd.hpp"
#include "binsrv/event/protocol_traits.hpp"

namespace binsrv::event {

class [[nodiscard]] reader_context {
  friend class event;

public:
  reader_context(std::uint32_t encoded_server_version, bool verify_checksum,
                 replication_mode_type replication_mode);

  [[nodiscard]] std::uint32_t
  get_current_encoded_server_version() const noexcept {
    return encoded_server_version_;
  }
  [[nodiscard]] bool get_current_verify_checksum() const noexcept {
    return verify_checksum_;
  }

  [[nodiscard]] std::size_t
  get_current_post_header_length(code_type code) const noexcept;
  [[nodiscard]] std::uint32_t get_current_position() const noexcept {
    return position_;
  }

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

  [[nodiscard]] bool is_event_info_only() const noexcept {
    return info_only_event_;
  }

  void set_expect_ignorable_preamble_events() noexcept {
    expect_ignorable_preamble_events_ = true;
  }

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
  std::uint32_t encoded_server_version_;
  bool verify_checksum_;
  replication_mode_type replication_mode_;
  post_header_length_container post_header_lengths_{};
  std::uint32_t position_{0U};

  gtids::gtid transaction_gtid_{};
  std::uint32_t expected_transaction_length_{0U};
  std::uint32_t current_transaction_length_{0U};

  bool expect_ignorable_preamble_events_{false};
  bool info_only_event_{false};

  void process_event(const event &current_event);
  [[nodiscard]] bool
  process_event_in_rotate_artificial_expected_state(const event &current_event);
  [[nodiscard]] bool process_event_in_format_description_expected_state(
      const event &current_event);
  [[nodiscard]] bool
  process_event_in_previous_gtids_expected_state(const event &current_event);
  [[nodiscard]] bool
  process_event_in_gtid_log_expected_state(const event &current_event);
  [[nodiscard]] bool
  process_event_in_any_other_expected_state(const event &current_event);
  [[nodiscard]] bool
  process_event_in_rotate_or_stop_expected_state(const event &current_event);

  void validate_position(const common_header &common_header) const;
  void validate_position_and_advance(const common_header &common_header);
  void reset_position();

  void start_transaction(const event &current_event);
  void update_transaction(const common_header &common_header);
  void finish_transaction();

  [[nodiscard]] static const post_header_length_container &
  get_hardcoded_post_header_lengths(
      std::uint32_t encoded_server_version) noexcept;
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_READER_CONTEXT_HPP
