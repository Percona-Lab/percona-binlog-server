#ifndef BINSRV_LOG_SEVERITY_HPP
#define BINSRV_LOG_SEVERITY_HPP

#include "binsrv/log_severity_fwd.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <type_traits>

namespace binsrv {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// clang-format off
#define BINSRV_LOG_SEVERITY_X_SEQUENCE() \
  BINSRV_LOG_SEVERITY_X_MACRO(trace  ),  \
  BINSRV_LOG_SEVERITY_X_MACRO(debug  ),  \
  BINSRV_LOG_SEVERITY_X_MACRO(info   ),  \
  BINSRV_LOG_SEVERITY_X_MACRO(warning),  \
  BINSRV_LOG_SEVERITY_X_MACRO(error  ),  \
  BINSRV_LOG_SEVERITY_X_MACRO(fatal  )
// clang-format on

#define BINSRV_LOG_SEVERITY_X_MACRO(X) X
enum class log_severity : std::uint8_t {
  BINSRV_LOG_SEVERITY_X_SEQUENCE(),
  delimiter
};
#undef BINSRV_LOG_SEVERITY_X_MACRO

inline std::string_view to_string_view(log_severity level) noexcept {
  using namespace std::string_view_literals;
#define BINSRV_LOG_SEVERITY_X_MACRO(X) #X##sv
  static constexpr std::array labels{BINSRV_LOG_SEVERITY_X_SEQUENCE(), ""sv};
#undef BINSRV_LOG_SEVERITY_X_MACRO
  const auto index = static_cast<std::size_t>(
      static_cast<std::underlying_type_t<log_severity>>(
          std::min(log_severity::delimiter, level)));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  return labels[index];
}
#undef BINSRV_LOG_SEVERITY_X_SEQUENCE
// NOLINTEND(cppcoreguidelines-macro-usage)

} // namespace binsrv

#endif // BINSRV_LOG_SEVERITY_HPP
