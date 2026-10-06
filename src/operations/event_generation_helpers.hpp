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

#ifndef OPERATIONS_EVENT_GENERATION_HELPERS_HPP
#define OPERATIONS_EVENT_GENERATION_HELPERS_HPP

#include <cstdint>

#include "binsrv/events/composite_binlog_name_fwd.hpp"
#include "binsrv/events/event_fwd.hpp"
#include "binsrv/events/event_view_fwd.hpp"
#include "binsrv/events/reader_context_fwd.hpp"

#include "binsrv/gtids/gtid_set_fwd.hpp"

#include "util/semantic_version_fwd.hpp"

namespace operations {

void generate_rotate_event_ex(
    binsrv::events::event_storage &event_buffer, bool include_checksum,
    std::uint32_t offset, bool current_timestamp, std::uint32_t server_id,
    bool artificial, const binsrv::events::composite_binlog_name &binlog_name,
    std::uint64_t position);
[[nodiscard]] binsrv::events::event_view
generate_rotate_event(binsrv::events::event_storage &event_buffer,
                      const binsrv::events::reader_context &context,
                      std::uint32_t offset, bool current_timestamp,
                      std::uint32_t server_id, bool artificial,
                      const binsrv::events::composite_binlog_name &binlog_name,
                      std::uint64_t position);

void generate_format_description_event_ex(
    binsrv::events::event_storage &event_buffer,
    const util::semantic_version &server_version, std::uint32_t offset,
    std::uint32_t server_id, bool enable_checksum_algorithm, bool artificial);
[[nodiscard]] binsrv::events::event_view generate_format_description_event(
    binsrv::events::event_storage &event_buffer,
    const binsrv::events::reader_context &context, std::uint32_t offset,
    std::uint32_t server_id, bool enable_checksum_algorithm, bool artificial);

[[nodiscard]] binsrv::events::event_view
generate_previous_gtids_log_event(binsrv::events::event_storage &event_buffer,
                                  const binsrv::events::reader_context &context,
                                  std::uint32_t offset, std::uint32_t server_id,
                                  const binsrv::gtids::gtid_set &gtids);

} // namespace operations

#endif // OPERATIONS_EVENT_GENERATION_HELPERS_HPP
