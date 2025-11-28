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

#ifndef BINSRV_EVENT_PRESIOUS_GTIDS_LOG_POST_HEADER_IMPL_HPP
#define BINSRV_EVENT_PRESIOUS_GTIDS_LOG_POST_HEADER_IMPL_HPP

#include "binsrv/event/previous_gtids_log_post_header_impl_fwd.hpp" // IWYU pragma: export

#include "binsrv/event/empty_post_header.hpp"

namespace binsrv::event {

template <>
class [[nodiscard]] generic_post_header_impl<code_type::previous_gtids_log> {
public:
  using redirect_type = empty_post_header;
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_PRESIOUS_GTIDS_LOG_POST_HEADER_IMPL_HPP
