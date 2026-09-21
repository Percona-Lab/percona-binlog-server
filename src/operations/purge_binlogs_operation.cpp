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
#include "operations/purge_binlogs_operation.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "binsrv/main_config.hpp"
#include "binsrv/null_logger.hpp"
#include "binsrv/storage.hpp"

#include "binsrv/events/composite_binlog_name.hpp"

#include "binsrv/models/error_response.hpp"
#include "binsrv/models/response_status_type.hpp"
#include "binsrv/models/search_response.hpp"

#include "operations/basic_operation.hpp"
#include "operations/mode_type.hpp"
#include "operations/model_helpers.hpp"

#include "util/command_line_helpers_fwd.hpp"

namespace operations {

generic_operation<mode_type::purge_binlogs>::generic_operation(
    util::command_line_arg_view cmd_args)
    : basic_operation{cmd_args, expected_number_of_arguments} {}

[[nodiscard]] bool
generic_operation<mode_type::purge_binlogs>::execute() const {
  bool operation_successful{false};
  std::string result;

  try {
    const auto target_name{
        binsrv::events::composite_binlog_name::parse(get_binlog_name())};

    const binsrv::main_config config{get_config_file_path()};

    binsrv::storage storage{std::make_shared<binsrv::null_logger>(), config,
                            binsrv::storage_construction_mode_type::purging};

    const auto [removed_records, cleanup_warning_message] =
        storage.purge_binlogs(target_name);

    // The step-2 index rewrite has already committed by the time
    // 'purge_binlogs' returns normally; if its best-effort step-3
    // cleanup left orphan payload/metadata objects on disk, the
    // call reports it via a non-empty 'cleanup_warning_message' and
    // we surface that as a 'warning' status (instead of plain
    // 'success'), with the underlying error message attached, so the
    // operator knows the storage will need attention before the next
    // 'fetch' / 'pull' run.
    binsrv::models::search_response response;
    if (!cleanup_warning_message.empty()) {
      response = binsrv::models::search_response{
          binsrv::models::response_status_type::warning,
          cleanup_warning_message};
    }
    for (const auto &record : removed_records) {
      append_record_to_search_response(response, storage, record);
    }
    result = response.str();
    operation_successful = true;
  } catch (const std::exception &e) {
    const binsrv::models::error_response response{e.what()};
    result = response.str();
  }
  std::cout << result << '\n';
  return operation_successful;
}

} // namespace operations
