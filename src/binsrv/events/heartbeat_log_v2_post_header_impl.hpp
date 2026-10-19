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

#ifndef BINSRV_EVENTS_HEARTBEAT_LOG_V2_POST_HEADER_IMPL_HPP
#define BINSRV_EVENTS_HEARTBEAT_LOG_V2_POST_HEADER_IMPL_HPP

#include "binsrv/events/heartbeat_log_v2_post_header_impl_fwd.hpp" // IWYU pragma: export

#include "binsrv/events/empty_post_header.hpp"

namespace binsrv::events {

// HEARTBEAT_LOG_EVENT_V2 has an empty post header (post-header length 0 in the
// FORMAT_DESCRIPTION event); all of its data lives in the body.
template <>
class [[nodiscard]] generic_post_header_impl<code_type::heartbeat_log_v2> {
public:
  using redirect_type = empty_post_header;
};

} // namespace binsrv::events

#endif // BINSRV_EVENTS_HEARTBEAT_LOG_V2_POST_HEADER_IMPL_HPP
