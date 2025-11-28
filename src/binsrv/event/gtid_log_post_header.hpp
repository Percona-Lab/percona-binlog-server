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

#ifndef BINSRV_EVENT_GTID_LOG_POST_HEADER_HPP
#define BINSRV_EVENT_GTID_LOG_POST_HEADER_HPP

#include "binsrv/event/gtid_log_post_header_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <cstdint>

#include "binsrv/event/gtid_log_flag_type_fwd.hpp"

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/uuid.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

class [[nodiscard]] gtid_log_post_header {
public:
  static constexpr std::size_t size_in_bytes{42U};

  // https://github.com/mysql/mysql-server/blob/mysql-8.0.43/libbinlogevents/include/control_events.h#L1091
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.h#L1202
  static constexpr std::uint8_t expected_logical_ts_code{2U};

  explicit gtid_log_post_header(util::const_byte_span portion);

  [[nodiscard]] std::uint8_t get_flags_raw() const noexcept { return flags_; }
  [[nodiscard]] gtid_log_flag_set get_flags() const noexcept;
  [[nodiscard]] std::string get_readable_flags() const;

  [[nodiscard]] const gtids::uuid_storage &get_uuid_raw() const noexcept {
    return uuid_;
  }
  [[nodiscard]] gtids::uuid get_uuid() const noexcept;
  [[nodiscard]] std::string get_readable_uuid() const;

  [[nodiscard]] std::int64_t get_gno_raw() const noexcept { return gno_; }

  [[nodiscard]] std::uint8_t get_logical_ts_code_raw() const noexcept {
    return logical_ts_code_;
  }

  [[nodiscard]] std::int64_t get_last_committed_raw() const noexcept {
    return last_committed_;
  }

  [[nodiscard]] std::int64_t get_sequence_number_raw() const noexcept {
    return sequence_number_;
  }

private:
  // the members are deliberately reordered for better packing
  std::uint8_t flags_{};            // 0
  std::uint8_t logical_ts_code_{};  // 3
  gtids::uuid_storage uuid_{};      // 1
  std::int64_t gno_{};              // 2
  std::int64_t last_committed_{};   // 4
  std::int64_t sequence_number_{};  // 5
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_GTID_LOG_POST_HEADER_HPP
