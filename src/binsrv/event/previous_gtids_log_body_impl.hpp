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

#ifndef BINSRV_EVENT_PREVIOUS_GTIDS_LOG_BODY_IMPL_HPP
#define BINSRV_EVENT_PREVIOUS_GTIDS_LOG_BODY_IMPL_HPP

#include "binsrv/event/previous_gtids_log_body_impl_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <cstdint>

#include <boost/container/small_vector.hpp>

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid_set_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

template <>
class [[nodiscard]] generic_body_impl<code_type::previous_gtids_log> {
public:
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.h#L1354

  explicit generic_body_impl(util::const_byte_span portion);

  [[nodiscard]] const gtids::gtid_set_storage &get_gtids_raw() const noexcept {
    return gtids_;
  }
  [[nodiscard]] gtids::gtid_set get_gtids() const;
  [[nodiscard]] std::string get_readable_gtids() const;

private:
  gtids::gtid_set_storage gtids_;
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_PREVIOUS_GTIDS_LOG_BODY_IMPL_HPP
