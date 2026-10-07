// Copyright (c) 2026 Percona and/or its affiliates.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 2.0,
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

#include "binsrv/indexed_event_block.hpp"

#include <cassert>
#include <cstddef>
#include <string>
#include <utility>

#include "binsrv/events/common_header_view.hpp"

#include "util/dynamic_byte_buffer_fwd.hpp"

namespace binsrv {

indexed_event_block::indexed_event_block(util::dynamic_byte_buffer buffer)
    : buffer_(std::move(buffer)) {
  auto offset{0UZ};
  while (true) {
    const auto remaining_size{std::size(buffer_) - offset};
    if (remaining_size < events::common_header_view_base::size_in_bytes) {
      break;
    }

    const events::common_header_view header{
        util::const_byte_span{buffer_}.subspan(
            offset, events::common_header_view_base::size_in_bytes)};
    const auto event_size{header.get_event_size_raw()};

    assert(event_size >= events::common_header_view_base::size_in_bytes);

    if (event_size > remaining_size) {
      break;
    }

    // TODO: consider performing checksum validation here
    index_.push_back({offset, event_size});
    offset += event_size;
  }
  // 'offset' now points past the last complete event; the remaining bytes
  // (if any) stay in 'buffer_' and are exposed via 'get_unparsed_tail()' so
  // the caller can prepend them to the next fetch instead of re-reading them.
  actual_size_ = offset;
}

} // namespace binsrv
