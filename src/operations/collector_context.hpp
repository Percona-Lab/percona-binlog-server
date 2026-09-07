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

#ifndef OPERATIONS_COLLECTOR_CONTEXT_HPP
#define OPERATIONS_COLLECTOR_CONTEXT_HPP

#include "operations/collector_context_fwd.hpp" // IWYU pragma: export

#include <atomic>
#include <string_view>

#include <boost/asio/ts/netfwd.hpp>

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/main_config_fwd.hpp"
#include "binsrv/storage_fwd.hpp"

#include "binsrv/events/event_view_fwd.hpp"
#include "binsrv/events/reader_context_fwd.hpp"

#include "easymysql/connection_fwd.hpp"
#include "easymysql/library_fwd.hpp"

#include "util/command_line_helpers_fwd.hpp"

namespace operations {

class collector_context {
public:
  // deliberately passing by value as we will be moving from these objects
  collector_context(
      easymysql::connection_replication_mode_type connection_replication_mode,
      binsrv::main_config_ptr config, binsrv::basic_logger_ptr logger);

  collector_context(const collector_context &) = delete;
  collector_context &operator=(const collector_context &) = delete;
  collector_context(collector_context &&) = delete;
  collector_context &operator=(collector_context &&) = delete;
  ~collector_context();

  [[nodiscard]] static binsrv::basic_logger_ptr
  initialize_console_logger(util::command_line_arg_view cmd_args);
  static void
  reinitialize_logger_from_config(binsrv::basic_logger_ptr &logger,
                                  const binsrv::main_config &config);

  // deliberately not marked as [[nodiscard]]
  bool receive_binlog_events(const boost::asio::io_context &io_ctx);

private:
  easymysql::connection_replication_mode_type connection_replication_mode_;
  binsrv::main_config_ptr config_;
  binsrv::basic_logger_ptr logger_;
  binsrv::storage_ptr storage_{};
  easymysql::library_ptr mysql_lib_{};

  [[nodiscard]] bool
  open_connection_and_switch_to_replication(easymysql::connection &connection);
  void rewrite_and_process_binlog_event(
      const binsrv::events::event_view &current_event_v,
      binsrv::events::reader_context &context);
  void process_binlog_event(const binsrv::events::event_view &current_event_v,
                            binsrv::events::reader_context &context);
  void process_artificial_rotate_event(
      const binsrv::events::event_view &current_event_v);
  void process_rotate_or_stop_event();
};

} // namespace operations

#endif // OPERATIONS_COLLECTOR_CONTEXT_HPP
