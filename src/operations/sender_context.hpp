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

#ifndef OPERATIONS_SENDER_CONTEXT_HPP
#define OPERATIONS_SENDER_CONTEXT_HPP

#include "operations/sender_context_fwd.hpp" // IWYU pragma: export

#include <cstddef>

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/indexed_event_block_fwd.hpp"
#include "binsrv/storage_fwd.hpp"

#include "binsrv/events/composite_binlog_name.hpp"

#include "util/byte_range.hpp"
#include "util/byte_span_fwd.hpp"

namespace operations {

class sender_context {
public:
  // deliberately passing by value as we will be moving from these objects
  sender_context(binsrv::basic_logger_ptr logger, binsrv::storage_ptr storage,
                 std::size_t block_size);

  sender_context(const sender_context &) = delete;
  sender_context &operator=(const sender_context &) = delete;
  sender_context(sender_context &&) = delete;
  sender_context &operator=(sender_context &&) = delete;
  ~sender_context();

  // returns false on error
  // returns true and sets the event span to a non-empty value on success
  // returns true and sets the event span to an empty object on EOF
  [[nodiscard]] bool get_event(util::const_byte_span &event);

private:
  binsrv::basic_logger_ptr logger_{};
  binsrv::storage_ptr storage_{};

  std::size_t block_size_{};
  binsrv::events::composite_binlog_name binlog_name_{};
  util::byte_range range_{};
  binsrv::indexed_event_block_ptr event_block_{};
  std::size_t event_index_{};
};

} // namespace operations

#endif // OPERATIONS_SENDER_CONTEXT_HPP
