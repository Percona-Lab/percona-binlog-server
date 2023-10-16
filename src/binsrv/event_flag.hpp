#ifndef BINSRV_EVENT_FLAG_HPP
#define BINSRV_EVENT_FLAG_HPP

#include "binsrv/event_flag_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include "util/flag_set.hpp"

namespace binsrv {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// Event flags copied from
// https://github.com/mysql/mysql-server/blob/mysql-8.0.33/sql/log_event.h#L253
// clang-format off
#define BINSRV_EVENT_FLAG_XY_SEQUENCE() \
  BINSRV_EVENT_FLAG_XY_MACRO(thread_specific, 0x004U), \
  BINSRV_EVENT_FLAG_XY_MACRO(suppress_use   , 0x008U), \
  BINSRV_EVENT_FLAG_XY_MACRO(artificial     , 0x020U), \
  BINSRV_EVENT_FLAG_XY_MACRO(relay_log      , 0x040U), \
  BINSRV_EVENT_FLAG_XY_MACRO(ignorable      , 0x080U), \
  BINSRV_EVENT_FLAG_XY_MACRO(no_filter      , 0x100U), \
  BINSRV_EVENT_FLAG_XY_MACRO(mts_isolate    , 0x200U)
// clang-format on

#define BINSRV_EVENT_FLAG_XY_MACRO(X, Y) X = Y
enum class event_flag : std::uint16_t {
  BINSRV_EVENT_FLAG_XY_SEQUENCE(),
  delimiter
};
#undef BINSRV_EVENT_FLAG_XY_MACRO

inline std::string_view to_string_view(event_flag code) noexcept {
  using namespace std::string_view_literals;
  using nv_pair = std::pair<event_flag, std::string_view>;
#define BINSRV_EVENT_FLAG_XY_MACRO(X, Y)                                       \
  nv_pair { event_flag::X, #X##sv }
  static constexpr std::array labels{BINSRV_EVENT_FLAG_XY_SEQUENCE(),
                                     nv_pair{event_flag::delimiter, ""sv}};
#undef BINSRV_EVENT_FLAG_XY_MACRO
  return std::ranges::find(labels, std::min(event_flag::delimiter, code),
                           &nv_pair::first)
      ->second;
}
#undef BINSRV_EVENT_FLAG_XY_SEQUENCE
// NOLINTEND(cppcoreguidelines-macro-usage)

} // namespace binsrv

#endif // BINSRV_EVENT_FLAG_HPP
