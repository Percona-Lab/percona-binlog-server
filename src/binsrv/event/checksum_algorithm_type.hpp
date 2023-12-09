#ifndef BINSRV_EVENT_CHECKSUM_ALGORITHM_TYPE_HPP
#define BINSRV_EVENT_CHECKSUM_ALGORITHM_TYPE_HPP

#include "binsrv/event/checksum_algorithm_type_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace binsrv::event {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// Checksum algorithm type codes copied from
// https://github.com/mysql/mysql-server/blob/mysql-8.0.35/libbinlogevents/include/binlog_event.h#L425
// clang-format off
#define BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_SEQUENCE() \
  BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO(off  ,  0), \
  BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO(crc32,  1)
// clang-format on

#define BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO(X, Y) X = Y
enum class checksum_algorithm_type : std::uint8_t {
  BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_SEQUENCE(),
  delimiter
};
#undef BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO

inline std::string_view to_string_view(checksum_algorithm_type code) noexcept {
  using namespace std::string_view_literals;
  using nv_pair = std::pair<checksum_algorithm_type, std::string_view>;
#define BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO(X, Y)                          \
  nv_pair { checksum_algorithm_type::X, #X##sv }
  static constexpr std::array labels{
      BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_SEQUENCE(),
      nv_pair{checksum_algorithm_type::delimiter, ""sv}};
#undef BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_MACRO
  return std::ranges::find(labels,
                           std::min(checksum_algorithm_type::delimiter, code),
                           &nv_pair::first)
      ->second;
}
#undef BINSRV_CHECKSUM_ALGORITHM_TYPE_XY_SEQUENCE
// NOLINTEND(cppcoreguidelines-macro-usage)

} // namespace binsrv::event

#endif // BINSRV_EVENT_CHECKSUM_ALGORITHM_TYPE_HPP
