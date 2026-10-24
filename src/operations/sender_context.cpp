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
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "binsrv/basic_logger.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/storage.hpp"

#include "binsrv/events/protocol_traits_fwd.hpp"

#include "operations/sender_single_file_context.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace operations {

sender_context::sender_context(binsrv::basic_logger_ptr logger,
                               binsrv::storage_ptr storage,
                               std::size_t block_size,
                               std::string_view binlog_name,
                               std::uint64_t position,
                               bool session_source_binlog_checksum)
    : logger_{std::move(logger)}, storage_{std::move(storage)},
      construction_block_size_{block_size},
      current_binlog_name_{
          binlog_name.empty()
              ? binsrv::events::composite_binlog_name{}
              : binsrv::events::composite_binlog_name::parse(binlog_name)},
      current_position_{position},
      current_binlog_checksum_{session_source_binlog_checksum} {
  assert(logger_);
  assert(storage_);
}

sender_context::~sender_context() = default;

[[nodiscard]] event_fetching_result_type
sender_context::get_event(util::const_byte_span &event) {
  // resolving real binlog name
  if (current_binlog_name_.is_empty()) {
    current_binlog_name_ = storage_->get_next_binlog_name(current_binlog_name_);
    // the result may still be empty if the storage is empty
    if (current_binlog_name_.is_empty()) {
      logger_->log(binsrv::log_severity::info,
                   "sender : waiting for first binlog in empty storage");
      event = {};
      return event_fetching_result_type::waiting;
    }
    logger_->log_format(binsrv::log_severity::info,
                        "sender : empty binlog name resolved to {}",
                        current_binlog_name_.str());
  }

  while (true) {
    if (!single_file_context_) {
      single_file_context_ = std::make_unique<sender_single_file_context>(
          logger_, storage_, construction_block_size_, current_binlog_name_,
          current_position_, current_binlog_checksum_);
      logger_->log_format(binsrv::log_severity::info,
                          "sender : switched to a new binlog file {}",
                          current_binlog_name_.str());
    }

    util::const_byte_span local_event{};
    const auto result{single_file_context_->get_event(local_event)};
    switch (result) {
    case event_fetching_result_type::success:
      event = local_event;
      return event_fetching_result_type::success;

    case event_fetching_result_type::waiting:
      event = {};
      return event_fetching_result_type::waiting;

    case event_fetching_result_type::eof:
      current_binlog_checksum_ =
          single_file_context_->is_checksum_enabled_in_format_description();
      current_position_ = binsrv::events::magic_binlog_offset;
      current_binlog_name_ =
          storage_->get_next_binlog_name(current_binlog_name_);
      if (current_binlog_name_.is_empty()) {
        util::exception_location().raise<std::logic_error>(
            "storage returned empty binlog name after non-active EOF");
      }

      single_file_context_.reset();
      break;
    }
  }
}

} // namespace operations
