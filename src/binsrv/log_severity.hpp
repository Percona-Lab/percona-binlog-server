#ifndef BINSRV_LOG_SEVERITY_HPP
#define BINSRV_LOG_SEVERITY_HPP

#include "binsrv/log_severity_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <array>
#include <istream>
#include <ostream>
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

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_ostream<Char, Traits> &
operator<<(std::basic_ostream<Char, Traits> &output, log_severity level) {
  return output << to_string_view(level);
}

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_istream<Char, Traits> &
operator>>(std::basic_istream<Char, Traits> &input, log_severity &level) {
  std::string level_str;
  input >> level_str;
  if (!input) {
    return input;
  }
  using index_type = std::underlying_type_t<log_severity>;
  index_type index = 0;
  const auto max_index = static_cast<index_type>(log_severity::delimiter);
  while (index < max_index &&
         to_string_view(static_cast<log_severity>(index)) != level_str) {
    ++index;
  }
  if (index < max_index) {
    level = static_cast<log_severity>(index);
  } else {
    input.setstate(std::ios_base::failbit);
  }
  return input;
}

} // namespace binsrv

#endif // BINSRV_LOG_SEVERITY_HPP
