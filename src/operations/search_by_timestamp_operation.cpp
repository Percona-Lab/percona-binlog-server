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
#include "operations/search_by_timestamp_operation.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "binsrv/main_config.hpp"
#include "binsrv/null_logger.hpp"
#include "binsrv/storage.hpp"

#include "binsrv/models/error_response.hpp"
#include "binsrv/models/search_response.hpp"

#include "operations/basic_operation.hpp"
#include "operations/mode_type.hpp"
#include "operations/model_helpers.hpp"

#include "util/command_line_helpers_fwd.hpp"
#include "util/ctime_timestamp.hpp"

namespace operations {

generic_operation<mode_type::search_by_timestamp>::generic_operation(
    util::command_line_arg_view cmd_args)
    : basic_operation{cmd_args, expected_number_of_arguments} {}

[[nodiscard]] bool
generic_operation<mode_type::search_by_timestamp>::execute() const {
  bool operation_successful{false};
  std::string result;

  try {
    util::ctime_timestamp timestamp;
    if (!util::ctime_timestamp::try_parse(get_timestamp(), timestamp)) {
      throw std::runtime_error("Invalid timestamp format");
    }

    const binsrv::main_config config{get_config_file_path()};

    const binsrv::storage storage{
        std::make_shared<binsrv::null_logger>(), config,
        binsrv::storage_construction_mode_type::querying_only};

    binsrv::models::search_response response;
    const auto &binlog_records{storage.get_binlog_records()};
    if (binlog_records.empty()) {
      throw std::runtime_error("Binlog storage is empty");
    }
    for (const auto &record : binlog_records) {
      // break when we find a binlog file with min timestamp greater
      // than the provided one
      if (record.timestamps.get_min_timestamp() > timestamp) {
        break;
      }
      append_record_to_search_response(response, storage, record);
    }
    if (response.root().get<"result">().empty()) {
      throw std::runtime_error("Timestamp is too old");
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
