#include "binsrv/event_header.hpp"

#include <cassert>
// probably a bug in IWYU: <concepts> is required by std::integral
// TODO: check if the same bug exust in clang-17
#include <concepts> // IWYU pragma: keep
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <boost/date_time/posix_time/conversion.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>
#include <boost/endian/conversion.hpp>

#include "binsrv/event_flag.hpp"
#include "binsrv/event_type.hpp"

#include "easymysql/binlog_fwd.hpp"

#include "util/exception_location_helpers.hpp"
#include "util/flag_set.hpp"

namespace {

template <std::integral T>
[[nodiscard]] inline T
read_fixed_int_from_span(easymysql::binlog_stream_span portion) noexcept {
  assert(std::size(portion) == sizeof(T));
  T result;
  std::memcpy(&result, std::data(portion), sizeof(T));
  // A fixed-length unsigned integer stores its value in a series of
  // bytes with the least significant byte first.
  // TODO: in c++23 use std::byteswap()
  return boost::endian::little_to_native(result);
}

template <std::integral T>
bool extract_fixed_int_from_stream(easymysql::binlog_stream_span &remainder,
                                   T &value) noexcept {
  if (std::size(remainder) < sizeof(T)) {
    return false;
  }
  value = read_fixed_int_from_span<T>(remainder.subspan(0U, sizeof(T)));
  remainder = remainder.subspan(sizeof(T));
  return true;
}

} // anonymous namespace

namespace binsrv {

event_header::event_header(easymysql::binlog_stream_span portion) {
  /*
    https://github.com/mysql/mysql-server/blob/mysql-8.0.33/libbinlogevents/src/binlog_event.cpp#L197

    The first 19 bytes in the header is as follows:
      +============================================+
      | member_variable               offset : len |
      +============================================+
      | when.tv_sec                        0 : 4   |
      +--------------------------------------------+
      | type_code       EVENT_TYPE_OFFSET(4) : 1   |
      +--------------------------------------------+
      | server_id       SERVER_ID_OFFSET(5)  : 4   |
      +--------------------------------------------+
      | data_written    EVENT_LEN_OFFSET(9)  : 4   |
      +--------------------------------------------+
      | log_pos           LOG_POS_OFFSET(13) : 4   |
      +--------------------------------------------+
      | flags               FLAGS_OFFSET(17) : 2   |
      +--------------------------------------------+
      | extra_headers                     19 : x-19|
      +============================================+
   */
  auto remainder = portion;
  auto result =
      extract_fixed_int_from_stream(remainder, timestamp_) &&
      extract_fixed_int_from_stream(remainder, type_code_) &&
      extract_fixed_int_from_stream(remainder, server_id_) &&
      extract_fixed_int_from_stream(remainder, event_size_) &&
      extract_fixed_int_from_stream(remainder, next_event_position_) &&
      extract_fixed_int_from_stream(remainder, flags_);
  if (!result) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid event header");
  }
  if (get_type_code_raw() >=
      static_cast<std::underlying_type_t<event_type>>(event_type::delimiter)) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid event header");
  }
}

[[nodiscard]] std::string event_header::get_readable_timestamp() const {
  return boost::posix_time::to_simple_string(
      boost::posix_time::from_time_t(get_timestamp()));
}

[[nodiscard]] std::string_view
event_header::get_readable_type_code() const noexcept {
  return to_string_view(get_type_code());
}

[[nodiscard]] event_flag_set event_header::get_flags() const noexcept {
  return event_flag_set{get_flags_raw()};
}

[[nodiscard]] std::string event_header::get_readable_flags() const {
  return to_string(get_flags());
}

} // namespace binsrv
