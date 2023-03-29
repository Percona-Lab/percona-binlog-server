#ifndef BINSRV_BASIC_LOGGER_HPP
#define BINSRV_BASIC_LOGGER_HPP

#include "binsrv/basic_logger_fwd.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <type_traits>

namespace binsrv {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define BINSRV_BASIC_LOGGER_X_SEQUENCE()                                       \
  BINSRV_BASIC_LOGGER_X_MACRO(trace), BINSRV_BASIC_LOGGER_X_MACRO(debug),      \
      BINSRV_BASIC_LOGGER_X_MACRO(info), BINSRV_BASIC_LOGGER_X_MACRO(warning), \
      BINSRV_BASIC_LOGGER_X_MACRO(error), BINSRV_BASIC_LOGGER_X_MACRO(fatal)

#define BINSRV_BASIC_LOGGER_X_MACRO(X) X
enum class log_severity { BINSRV_BASIC_LOGGER_X_SEQUENCE(), delimiter };
#undef BINSRV_BASIC_LOGGER_X_MACRO

#define BINSRV_BASIC_LOGGER_X_MACRO(X) #X##sv
inline std::string_view to_string_view(log_severity level) noexcept {
  using namespace std::string_view_literals;
  static constexpr std::array labels{BINSRV_BASIC_LOGGER_X_SEQUENCE(), ""sv};
  const auto index = static_cast<std::size_t>(
      static_cast<std::underlying_type_t<log_severity>>(
          std::min(log_severity::delimiter, level)));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  return labels[index];
}
#undef BINSRV_BASIC_LOGGER_X_MACRO
#undef BINSRV_BASIC_LOGGER_X_SEQUENCE
// NOLINTEND(cppcoreguidelines-macro-usage)

class basic_logger {
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
  basic_logger() = default;

private:
  log_severity min_level_{log_severity::info};

  virtual void do_log(std::string_view message) = 0;
};

} // namespace binsrv

#endif // BINSRV_BASIC_LOGGER_HPP
