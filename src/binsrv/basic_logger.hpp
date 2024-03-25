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

#ifndef BINSRV_BASIC_LOGGER_HPP
#define BINSRV_BASIC_LOGGER_HPP

#include "binsrv/basic_logger_fwd.hpp" // IWYU pragma: export

#include <string_view>

#include "binsrv/log_severity_fwd.hpp"

namespace binsrv {

class [[nodiscard]] basic_logger {
public:
  basic_logger(const basic_logger &) = delete;
  basic_logger &operator=(const basic_logger &) = delete;
  basic_logger(basic_logger &&) = delete;
  basic_logger &operator=(basic_logger &&) = delete;

  virtual ~basic_logger() = default;

  [[nodiscard]] log_severity get_min_level() const noexcept {
    return min_level_;
  }
  void set_min_level(log_severity min_level) noexcept {
    min_level_ = min_level;
  }

  void log(log_severity level, std::string_view message);

protected:
  explicit basic_logger(log_severity min_level) noexcept;

private:
  log_severity min_level_;

  virtual void do_log(std::string_view message) = 0;
};

} // namespace binsrv

#endif // BINSRV_BASIC_LOGGER_HPP
