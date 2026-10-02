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

#include "operations/sender_context.hpp"

#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>

#include "binsrv/basic_logger.hpp"
#include "binsrv/indexed_event_block.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/storage.hpp"

#include "binsrv/events/protocol_traits_fwd.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"

namespace operations {

sender_context::sender_context(binsrv::basic_logger_ptr logger,
                               binsrv::storage_ptr storage,
                               std::size_t block_size)
    : logger_{std::move(logger)}, storage_{std::move(storage)},
      block_size_{block_size},
      range_{binsrv::events::magic_binlog_offset, block_size_} {
  assert(block_size_ > 0UZ);
  assert(storage_);
  assert(logger_);
}

sender_context::~sender_context() = default;

[[nodiscard]] bool sender_context::get_event(util::const_byte_span &event) {
  // if this is the very first call when 'event_block_' is not yet set or we
  // have consumed all events in the current block
  if (!event_block_ || event_index_ == event_block_->get_number_of_events()) {
    // early reset to free memory from the previous event block
    event_block_.reset();

    util::dynamic_byte_buffer buffer{};
    // on success both 'binlog_name_' and 'range_' will be updated
    if (!storage_->fetch_event_block(binlog_name_, range_, buffer)) {
      return false;
    }
    if (buffer.empty()) {
      logger_->log(binsrv::log_severity::info, "sender : fetched EOF");
      // On EOF, 'storage::fetch_event_block()' leaves 'range_' with length 0,
      // and unchanged offset. Restore the length while
      // keeping 'binlog_name_' and the current offset so that a subsequent
      // call resumes polling at the same position.
      range_ = util::byte_range{range_.get_offset(), block_size_};

      // setting the event span to an empty object to indicate EOF
      event = util::const_byte_span{};
      return true; // EOF
    }
    logger_->log_format(binsrv::log_severity::info,
                        "sender : fetched event block of size {}, {}:{}",
                        std::size(buffer), binlog_name_.str(),
                        range_.to_string());
    event_block_ =
        std::make_unique<binsrv::indexed_event_block>(std::move(buffer));
    if (event_block_->is_empty()) {
      // in case when the received buffer is not empty but after parsing it has
      // no valid events, we should treat this situation as an error

      // TODO: double the length of the requested block and retry fetching
      return false;
    }
    event_index_ = 0UZ;
    logger_->log_format(
        binsrv::log_severity::info,
        "sender : parsed event block with {} events, actual size {} byte(s)",
        event_block_->get_number_of_events(), event_block_->get_actual_size());
    // preparing 'range_' for the next fetch
    range_ = util::byte_range{
        range_.get_offset() + event_block_->get_actual_size(), block_size_};
  }
  event = event_block_->get_event(event_index_);
  ++event_index_;
  return true;
}

} // namespace operations
