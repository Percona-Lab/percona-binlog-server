#ifndef BINSRV_LOG_SEVERITY_HPP
#define BINSRV_LOG_SEVERITY_HPP

#include "binsrv/log_severity_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <array>
#include <istream>
#include <ostream>
#include <string_view>
#include <type_traits>

#include "util/conversion_helpers.hpp"

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
  const auto index{
      util::enum_to_index(std::min(log_severity::delimiter, level))};
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
  std::size_t index{0U};
  const auto max_index = util::enum_to_index(log_severity::delimiter);
  while (index < max_index && to_string_view(util::index_to_enum<log_severity>(
                                  index)) != level_str) {
    ++index;
  }
  if (index < max_index) {
    level = util::index_to_enum<log_severity>(index);
  } else {
    input.setstate(std::ios_base::failbit);
  }
  return input;
}

} // namespace binsrv

#endif // BINSRV_LOG_SEVERITY_HPP
