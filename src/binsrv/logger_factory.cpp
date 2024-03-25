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

#include "binsrv/logger_factory.hpp"

#include <memory>

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/cout_logger.hpp"
#include "binsrv/file_logger.hpp"
#include "binsrv/logger_config.hpp"

namespace binsrv {
basic_logger_ptr logger_factory::create(const logger_config &config) {
  const auto level = config.get<"level">();
  if (!config.has_file()) {
    return std::make_shared<cout_logger>(level);
  }

  return std::make_shared<file_logger>(level, config.get<"file">());
}

} // namespace binsrv
