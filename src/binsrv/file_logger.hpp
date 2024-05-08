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

#ifndef BINSRV_FILE_LOGGER_HPP
#define BINSRV_FILE_LOGGER_HPP

#include <fstream>
#include <string>
#include <string_view>

#include "binsrv/basic_logger.hpp" // IWYU pragma: export
#include "binsrv/log_severity_fwd.hpp"

namespace binsrv {

class [[nodiscard]] file_logger final : public basic_logger {
public:
  file_logger(log_severity min_level, std::string_view file_name);

private:
  std::ofstream stream_;

  void do_log(std::string_view message) override;
};

} // namespace binsrv

#endif // BINSRV_FILE_LOGGER_HPP
