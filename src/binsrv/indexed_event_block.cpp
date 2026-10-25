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
#include <stdexcept>
#include <string>
#include <utility>

#include "binsrv/events/common_header_view.hpp"

#include "util/common_optional_types.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

indexed_event_block::indexed_event_block(util::dynamic_byte_buffer buffer,
                                         std::size_t max_events_to_parse)
    : buffer_(std::move(buffer)) {
  auto offset{0UZ};
  auto remaining_size{std::size(buffer_)};

  for (std::size_t events_parsed = 0; events_parsed < max_events_to_parse;
       ++events_parsed) {
    const auto event_size{extract_event_size_internal(offset)};

    if (!event_size.has_value()) {
      break;
    }

    if (*event_size < events::common_header_view_base::size_in_bytes) {
      util::exception_location().raise<std::runtime_error>(
          "in indexed_event_block constructor encountered an event with "
          "abnormal size");
    }

    if (*event_size > remaining_size) {
      break;
    }

    // TODO: consider performing checksum validation here
    index_.push_back({offset, *event_size});
    offset += *event_size;
    remaining_size -= *event_size;
  }
}

util::optional_size_t indexed_event_block::extract_event_size_internal(
    std::size_t offset) const noexcept {
  if (offset + events::common_header_view_base::size_in_bytes >
      std::size(buffer_)) {
    return {};
  }
  const events::common_header_view header{
      util::const_byte_span{buffer_}.subspan(
          offset, events::common_header_view_base::size_in_bytes)};
  return header.get_event_size_raw();
}

} // namespace binsrv
