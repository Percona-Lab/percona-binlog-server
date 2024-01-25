#ifndef BINSRV_EVENT_CODE_TYPE_HPP
#define BINSRV_EVENT_CODE_TYPE_HPP

#include "binsrv/event/code_type_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace binsrv::event {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// Event type codes copied from
// https://github.com/mysql/mysql-server/blob/mysql-8.0.36/libbinlogevents/include/binlog_event.h#L274
// clang-format off
#define BINSRV_EVENT_CODE_TYPE_XY_SEQUENCE() \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(unknown            ,  0), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(start_v3           ,  1), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(query              ,  2), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(stop               ,  3), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(rotate             ,  4), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(intvar             ,  5), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(slave              ,  7), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(append_block       ,  9), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(delete_file        , 11), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(rand               , 13), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(user_var           , 14), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(format_description , 15), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(xid                , 16), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(begin_load_query   , 17), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(execute_load_query , 18), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(table_map          , 19), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(write_rows_v1      , 23), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(update_rows_v1     , 24), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(delete_rows_v1     , 25), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(incident           , 26), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(heartbeat_log      , 27), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(ignorable_log      , 28), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(rows_query_log     , 29), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(write_rows         , 30), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(update_rows        , 31), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(delete_rows        , 32), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(gtid_log           , 33), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(anonymous_gtid_log , 34), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(previous_gtids_log , 35), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(transaction_context, 36), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(view_change        , 37), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(xa_prepare_log     , 38), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(partial_update_rows, 39), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(transaction_payload, 40), \
  BINSRV_EVENT_CODE_TYPE_XY_MACRO(heartbeat_log_v2   , 41)
// clang-format on

#define BINSRV_EVENT_CODE_TYPE_XY_MACRO(X, Y) X = Y
enum class code_type : std::uint8_t {
  BINSRV_EVENT_CODE_TYPE_XY_SEQUENCE(),
  delimiter
};
#undef BINSRV_EVENT_CODE_TYPE_XY_MACRO

inline std::string_view to_string_view(code_type code) noexcept {
  using namespace std::string_view_literals;
  using nv_pair = std::pair<code_type, std::string_view>;
#define BINSRV_EVENT_CODE_TYPE_XY_MACRO(X, Y)                                  \
  nv_pair { code_type::X, #X##sv }
  static constexpr std::array labels{BINSRV_EVENT_CODE_TYPE_XY_SEQUENCE(),
                                     nv_pair{code_type::delimiter, ""sv}};
#undef BINSRV_EVENT_CODE_TYPE_XY_MACRO
  return std::ranges::find(labels, std::min(code_type::delimiter, code),
                           &nv_pair::first)
      ->second;
}
#undef BINSRV_EVENT_CODE_TYPE_XY_SEQUENCE
// NOLINTEND(cppcoreguidelines-macro-usage)

} // namespace binsrv::event

#endif // BINSRV_EVENT_CODE_TYPE_HPP
