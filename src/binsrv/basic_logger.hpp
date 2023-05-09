#ifndef BINSRV_BASIC_LOGGER_HPP
#define BINSRV_BASIC_LOGGER_HPP

#include "binsrv/basic_logger_fwd.hpp"

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
