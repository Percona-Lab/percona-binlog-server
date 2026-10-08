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

#ifndef BINSRV_LOGGER_FACTORY_HPP
#define BINSRV_LOGGER_FACTORY_HPP

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/logger_config_fwd.hpp"

namespace binsrv {

struct [[nodiscard]] logger_creation_options {
  // keep the existing content of the log file (if configured) instead of
  // removing it
  bool keep_file_content{false};
  // write to the standard error stream instead of the standard output if no
  // log file is configured
  bool use_standard_error{false};
};

struct [[nodiscard]] logger_factory {
  [[nodiscard]] static basic_logger_ptr
  create(const logger_config &config,
         const logger_creation_options &options = {});
};

} // namespace binsrv

#endif // BINSRV_LOGGER_FACTORY_HPP
