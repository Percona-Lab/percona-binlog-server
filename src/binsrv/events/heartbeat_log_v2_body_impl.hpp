// Copyright (c) 2026 Percona and/or its affiliates.
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

#ifndef BINSRV_EVENTS_HEARTBEAT_LOG_V2_BODY_IMPL_HPP
#define BINSRV_EVENTS_HEARTBEAT_LOG_V2_BODY_IMPL_HPP

#include "binsrv/events/heartbeat_log_v2_body_impl_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <boost/container/small_vector.hpp>

#include "binsrv/events/composite_binlog_name_fwd.hpp"

#include "util/byte_span.hpp"

namespace binsrv::events {

// Body of HEARTBEAT_LOG_EVENT_V2. The body is a TLV (type / length / value)
// stream of MySQL length-encoded ("packed") integers that carries the current
// binlog file name and the current position within it:
//   https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/codecs/binary.cpp#L215
template <> class [[nodiscard]] generic_body_impl<code_type::heartbeat_log_v2> {
public:
  static constexpr std::size_t expected_max_binlog_name_length{64U};
  // TODO: in c++26 change to std::inplace_vector
  using binlog_storage =
      boost::container::small_vector<std::byte,
                                     expected_max_binlog_name_length>;

  generic_body_impl(const composite_binlog_name &binlog_name,
                    std::uint64_t position);
  explicit generic_body_impl(util::const_byte_span portion);

  [[nodiscard]] const binlog_storage &get_binlog_raw() const noexcept {
    return binlog_;
  }
  [[nodiscard]] std::string_view get_readable_binlog() const noexcept {
    return util::as_string_view(binlog_);
  }
  [[nodiscard]] composite_binlog_name get_parsed_binlog() const;
  [[nodiscard]] std::uint64_t get_position() const noexcept {
    return position_;
  }

  [[nodiscard]] std::size_t calculate_encoded_size() const noexcept;
  void encode_to(util::byte_span &destination) const;

  friend bool operator==(const generic_body_impl &first,
                         const generic_body_impl &second) = default;

private:
  binlog_storage binlog_{};
  std::uint64_t position_{};
};

} // namespace binsrv::events

#endif // BINSRV_EVENTS_HEARTBEAT_LOG_V2_BODY_IMPL_HPP
