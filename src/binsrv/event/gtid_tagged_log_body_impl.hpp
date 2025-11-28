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

#ifndef BINSRV_EVENT_GTID_TAGGED_LOG_BODY_IMPL_HPP
#define BINSRV_EVENT_GTID_TAGGED_LOG_BODY_IMPL_HPP

#include "binsrv/event/gtid_tagged_log_body_impl_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <cstdint>

#include "binsrv/event/gtid_log_flag_type_fwd.hpp"

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/uuid.hpp"

#include "util/bounded_string_storage.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/semantic_version_fwd.hpp"

namespace binsrv::event {

template <> class [[nodiscard]] generic_body_impl<code_type::gtid_tagged_log> {
public:
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.h#L1111

  explicit generic_body_impl(util::const_byte_span portion);

  [[nodiscard]] std::uint8_t get_flags_raw() const noexcept { return flags_; }
  [[nodiscard]] gtid_log_flag_set get_flags() const noexcept;
  [[nodiscard]] std::string get_readable_flags() const;

  [[nodiscard]] const gtids::uuid_storage &get_uuid_raw() const noexcept {
    return uuid_;
  }
  [[nodiscard]] gtids::uuid get_uuid() const noexcept;
  [[nodiscard]] std::string get_readable_uuid() const;

  [[nodiscard]] std::int64_t get_gno_raw() const noexcept { return gno_; }

  [[nodiscard]] const gtids::tag_storage &get_tag_raw() const noexcept {
    return tag_;
  }
  [[nodiscard]] std::string_view get_tag() const noexcept;

  [[nodiscard]] std::int64_t get_last_committed_raw() const noexcept {
    return last_committed_;
  }

  [[nodiscard]] std::int64_t get_sequence_number_raw() const noexcept {
    return sequence_number_;
  }

  [[nodiscard]] bool has_immediate_commit_timestamp() const noexcept {
    return immediate_commit_timestamp_ != unset_commit_timestamp;
  }
  [[nodiscard]] std::uint64_t
  get_immediate_commit_timestamp_raw() const noexcept {
    return immediate_commit_timestamp_;
  }
  [[nodiscard]] std::chrono::high_resolution_clock::time_point
  get_immediate_commit_timestamp() const noexcept {
    return std::chrono::high_resolution_clock::time_point{
        std::chrono::microseconds(get_immediate_commit_timestamp_raw())};
  }
  [[nodiscard]] std::string get_readable_immediate_commit_timestamp() const;

  [[nodiscard]] bool has_original_commit_timestamp() const noexcept {
    return original_commit_timestamp_ != unset_commit_timestamp;
  }
  [[nodiscard]] std::uint64_t
  get_original_commit_timestamp_raw() const noexcept {
    return original_commit_timestamp_;
  }

  [[nodiscard]] bool has_transaction_length() const noexcept {
    return transaction_length_ != unset_transaction_length;
  }
  [[nodiscard]] std::uint64_t get_transaction_length_raw() const noexcept {
    return transaction_length_;
  }

  [[nodiscard]] bool has_original_server_version() const noexcept {
    return original_server_version_ != unset_server_version;
  }
  [[nodiscard]] std::uint32_t get_original_server_version_raw() const noexcept {
    return original_server_version_;
  }
  [[nodiscard]] util::semantic_version
  get_original_server_version() const noexcept;
  [[nodiscard]] std::string get_readable_original_server_version() const;

  [[nodiscard]] bool has_immediate_server_version() const noexcept {
    return immediate_server_version_ != unset_server_version;
  }
  [[nodiscard]] std::uint32_t
  get_immediate_server_version_raw() const noexcept {
    return immediate_server_version_;
  }
  [[nodiscard]] util::semantic_version
  get_immediate_server_version() const noexcept;
  [[nodiscard]] std::string get_readable_immediate_server_version() const;

  [[nodiscard]] bool has_commit_group_ticket() const noexcept {
    return commit_group_ticket_ != unset_commit_group_ticket;
  }
  [[nodiscard]] std::uint64_t get_commit_group_ticket_raw() const noexcept {
    return commit_group_ticket_;
  }

private:
  static constexpr std::uint64_t unset_commit_timestamp{
      std::numeric_limits<std::uint64_t>::max()};
  static constexpr std::uint64_t unset_transaction_length{
      std::numeric_limits<std::uint64_t>::max()};
  static constexpr std::uint32_t unset_server_version{
      std::numeric_limits<std::uint32_t>::max()};
  static constexpr std::uint64_t unset_commit_group_ticket{
      std::numeric_limits<std::uint64_t>::max()};

  // the members are deliberately reordered for better packing
  std::uint8_t flags_{};                                             // 0
  gtids::uuid_storage uuid_{};                                       // 1
  std::int64_t gno_{};                                               // 2
  gtids::tag_storage tag_{};                                         // 3
  std::int64_t last_committed_{};                                    // 4
  std::int64_t sequence_number_{};                                   // 5
  std::uint64_t immediate_commit_timestamp_{unset_commit_timestamp}; // 6
  std::uint64_t original_commit_timestamp_{unset_commit_timestamp};  // 7
  std::uint64_t transaction_length_{unset_transaction_length};       // 8
  std::uint32_t original_server_version_{unset_server_version};      // 9
  std::uint32_t immediate_server_version_{unset_server_version};     // 10
  std::uint64_t commit_group_ticket_{unset_commit_group_ticket};     // 11

  void process_field_data(std::uint8_t field_id,
                          util::const_byte_span &remainder);
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_GTID_TAGGED_LOG_BODY_IMPL_HPP
