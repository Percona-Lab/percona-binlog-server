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

#include "binsrv/file_logger.hpp"

#include <filesystem>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>

#include "binsrv/log_severity_fwd.hpp"

#include "util/exception_location_helpers.hpp"

namespace binsrv {

file_logger::file_logger(log_severity min_level, std::string_view file_name,
                         bool keep_content)
    : basic_logger{min_level}, stream_{} {
  const std::filesystem::path file_path{file_name};
  if (!keep_content) {
    // just truncating the file (if it cannot be created, the error will be
    // reported below)
    const std::ofstream truncating_stream{file_path, std::ios_base::out |
                                                         std::ios_base::trunc};
  }
  // 'std::ios_base::app' makes every write go to the current end of the
  // file (O_APPEND)
  stream_.open(file_path, std::ios_base::out | std::ios_base::app);
  if (!stream_.is_open()) {
    util::exception_location().raise<std::runtime_error>(
        "unable to create \"" + std::string(file_name) + "\" file for logging");
  }
}

void file_logger::do_log(std::string_view message) {
  stream_ << message << '\n';
  stream_.flush();
}

} // namespace binsrv
