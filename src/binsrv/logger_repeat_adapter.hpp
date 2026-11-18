// Copyright (c) 2026 Percona and/or its affiliates.
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

#ifndef BINSRV_LOGGER_REPEAT_ADAPTER_HPP
#define BINSRV_LOGGER_REPEAT_ADAPTER_HPP

#include "binsrv/logger_repeat_adapter_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

#include <boost/container/small_vector.hpp>

#include "binsrv/basic_logger.hpp"
#include "binsrv/log_severity_fwd.hpp"

namespace binsrv {

// Wraps a 'basic_logger' and, in addition to regular logging, allows
// suppressing repeated messages. Messages passed to 'log_repeat()' /
// 'log_format_repeat()' are counted per 'group_id' and only the very first
// one in each group is actually logged until the group is reset with
// 'reset_group()'.
// Group IDs are expected to be small consecutive integers (e.g. 0, 1, 2) as
// they are used as indexes into a vector of counters that is stored inline
// (without heap allocation) for up to 'expected_number_of_groups' groups.
// Repeat counters are not synchronized, so an instance of this class is
// expected to be used from a single execution context (e.g. one replica
// session) at a time - the underlying logger itself is thread-safe.
class [[nodiscard]] logger_repeat_adapter {
public:
  explicit logger_repeat_adapter(basic_logger_ptr logger);

  [[nodiscard]] const basic_logger_ptr &get_underlying() const noexcept {
    return logger_;
  }

  void log(log_severity level, std::string_view message) {
    logger_->log(level, message);
  }

  template <typename... Args>
  void log_format(log_severity level, std::format_string<Args...> fmt,
                  Args &&...args) {
    logger_->log_format(level, fmt, std::forward<Args>(args)...);
  }

  void log_repeat(std::size_t group_id, log_severity level,
                  std::string_view message) {
    if (register_repeat(group_id)) {
      logger_->log(level, message);
    }
  }

  template <typename... Args>
  void log_format_repeat(std::size_t group_id, log_severity level,
                         std::format_string<Args...> fmt, Args &&...args) {
    if (register_repeat(group_id)) {
      logger_->log_format(level, fmt, std::forward<Args>(args)...);
    }
  }

  // makes the next message in this group logged again, returns the number of
  // messages counted in this group before the reset
  std::size_t reset_group(std::size_t group_id);

private:
  basic_logger_ptr logger_;
  static constexpr std::size_t expected_number_of_groups{4U};
  using counter_container =
      boost::container::small_vector<std::size_t, expected_number_of_groups>;
  counter_container counters_;

  // increments the counter of the group and returns true if this is the
  // first message in the group (the one that must be logged)
  [[nodiscard]] bool register_repeat(std::size_t group_id);
};

} // namespace binsrv

#endif // BINSRV_LOGGER_REPEAT_ADAPTER_HPP
