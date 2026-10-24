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
#include <cstdint>
#include <string_view>

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/storage_fwd.hpp"

#include "binsrv/events/composite_binlog_name.hpp"

#include "operations/sender_single_file_context_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace operations {

class sender_context {
public:
  // deliberately passing by value as we will be moving from these objects
  sender_context(binsrv::basic_logger_ptr logger, binsrv::storage_ptr storage,
                 std::size_t block_size, std::string_view binlog_name,
                 std::uint64_t position, bool session_source_binlog_checksum);

  sender_context(const sender_context &) = delete;
  sender_context &operator=(const sender_context &) = delete;
  sender_context(sender_context &&) = delete;
  sender_context &operator=(sender_context &&) = delete;
  ~sender_context();

  [[nodiscard]] event_fetching_result_type
  get_event(util::const_byte_span &event);

private:
  binsrv::basic_logger_ptr logger_{};
  binsrv::storage_ptr storage_{};

  std::size_t construction_block_size_{};

  binsrv::events::composite_binlog_name current_binlog_name_{};
  std::uint64_t current_position_{};
  bool current_binlog_checksum_{};

  sender_single_file_context_ptr single_file_context_{};
};

} // namespace operations

#endif // OPERATIONS_SENDER_CONTEXT_HPP
