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

#include <atomic>
#include <format>
#include <mutex>
#include <string_view>
#include <utility>

#include "binsrv/log_severity_fwd.hpp"

namespace binsrv {

class [[nodiscard]] basic_logger {
public:
  basic_logger(const basic_logger &) = delete;
  basic_logger &operator=(const basic_logger &) = delete;
  basic_logger(basic_logger &&) = delete;
  basic_logger &operator=(basic_logger &&) = delete;

  virtual ~basic_logger();

  [[nodiscard]] log_severity get_min_level() const noexcept {
    return min_level_.load(std::memory_order_relaxed);
  }
  void set_min_level(log_severity min_level) noexcept {
    min_level_.store(min_level, std::memory_order_relaxed);
  }

  void log(log_severity level, std::string_view message) {
    if (level >= get_min_level()) {
      log_internal(level, message);
    }
  }

  template <typename... Args>
  void log_format(log_severity level, std::format_string<Args...> fmt,
                  Args &&...args) {
    if (level >= get_min_level()) {
      log_internal(level, std::format(fmt, std::forward<Args>(args)...));
    }
  }

protected:
  explicit basic_logger(log_severity min_level) noexcept;

private:
  using atomic_log_severity = std::atomic<log_severity>;
  atomic_log_severity min_level_;
  std::mutex do_log_mutex_;

  // called only after the severity check has already been performed
  void log_internal(log_severity level, std::string_view message);

  // called with 'do_log_mutex_' held, so implementations need not synchronize
  virtual void do_log(std::string_view message) = 0;
};

} // namespace binsrv

#endif // BINSRV_BASIC_LOGGER_HPP
