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

#ifndef BINSRV_INDEXED_EVENT_BLOCK_HPP
#define BINSRV_INDEXED_EVENT_BLOCK_HPP

#include "binsrv/indexed_event_block_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <iterator>
#include <vector>

#include "util/byte_span_fwd.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"

namespace binsrv {

class [[nodiscard]] indexed_event_block {
public:
  struct index_record {
    std::size_t offset;
    std::size_t size;
  };
  using index_type = std::vector<index_record>;

  // Takes ownership of the provided buffer and parses event boundaries within
  // it. 'buffer' must start from a valid event position (where common header
  // starts) but may end in the middle of an event. After parsing,
  // 'get_actual_size()' will return the size of the parsed portion of the
  // buffer.

  // deliberately passing by value as we are moving from this argument
  explicit indexed_event_block(util::dynamic_byte_buffer buffer);

  indexed_event_block(const indexed_event_block &) = delete;
  indexed_event_block(indexed_event_block &&) = delete;
  indexed_event_block &operator=(const indexed_event_block &) = delete;
  indexed_event_block &operator=(indexed_event_block &&) = delete;

  ~indexed_event_block() = default;

  [[nodiscard]] std::size_t get_actual_size() const noexcept {
    return actual_size_;
  }
  [[nodiscard]] bool is_empty() const noexcept { return std::empty(index_); }

  [[nodiscard]] std::size_t get_number_of_events() const noexcept {
    return std::size(index_);
  }
  [[nodiscard]] util::const_byte_span
  get_event(std::size_t index) const noexcept {
    const auto &record{index_[index]};
    return util::const_byte_span{buffer_}.subspan(record.offset, record.size);
  }

  // Trailing bytes of the input buffer that did not form a complete event.
  // The caller is expected to carry them over and prepend to the next fetch.
  [[nodiscard]] util::const_byte_span get_unparsed_tail() const noexcept {
    return util::const_byte_span{buffer_}.subspan(actual_size_);
  }

private:
  util::dynamic_byte_buffer buffer_;
  std::size_t actual_size_{};
  index_type index_;
};

} // namespace binsrv

#endif // BINSRV_INDEXED_EVENT_BLOCK_HPP
