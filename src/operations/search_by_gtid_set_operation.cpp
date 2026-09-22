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
#include "operations/search_by_gtid_set_operation.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "binsrv/main_config.hpp"
#include "binsrv/null_logger.hpp"
#include "binsrv/storage.hpp"
#include "binsrv/storage_core.hpp"

#include "binsrv/models/error_response.hpp"
#include "binsrv/models/search_response.hpp"

#include "binsrv/gtids/gtid_set.hpp"

#include "operations/basic_operation.hpp"
#include "operations/mode_type.hpp"
#include "operations/model_helpers.hpp"

#include "util/command_line_helpers_fwd.hpp"

namespace operations {

generic_operation<mode_type::search_by_gtid_set>::generic_operation(
    util::command_line_arg_view cmd_args)
    : basic_operation{cmd_args, expected_number_of_arguments} {}

[[nodiscard]] bool
generic_operation<mode_type::search_by_gtid_set>::execute() const {
  bool operation_successful{false};
  std::string result;

  try {
    binsrv::gtids::gtid_set remaining_gtids{get_gtid_set()};

    const binsrv::main_config config{get_config_file_path()};

    const binsrv::storage storage{
        std::make_shared<binsrv::null_logger>(), config,
        binsrv::storage_construction_mode_type::querying_only};

    const auto &binlog_records{storage.get_binlog_records()};
    if (binlog_records.empty()) {
      throw std::runtime_error("Binlog storage is empty");
    }

    binsrv::models::search_response response;
    if (!storage.is_in_gtid_replication_mode()) {
      throw std::runtime_error("GTID set search is not supported in storages "
                               "created in position-based replication mode");
    }

    for (const auto &record : binlog_records) {
      if (remaining_gtids.is_empty()) {
        break;
      }
      if (!record.added_gtids.has_value()) {
        continue;
      }
      if (!binsrv::gtids::intersects(remaining_gtids, *record.added_gtids)) {
        continue;
      }
      remaining_gtids.subtract(*record.added_gtids);

      append_record_to_search_response(response, storage, record);
    }
    if (!remaining_gtids.is_empty()) {
      throw std::runtime_error("The specified GTID set cannot be covered");
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
