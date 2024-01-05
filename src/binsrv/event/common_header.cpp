#include "binsrv/event/common_header.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/align/align_up.hpp>

#include <boost/date_time/posix_time/conversion.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>

#include "binsrv/event/code_type.hpp"
#include "binsrv/event/flag_type.hpp"

#include "util/byte_span_extractors.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/flag_set.hpp"

namespace binsrv::event {

common_header::common_header(util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  /*
    https://github.com/mysql/mysql-server/blob/mysql-8.0.35/libbinlogevents/src/binlog_event.cpp#L197

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

  // TODO: initialize size_in_bytes directly based on the sum of fields
  // widths instead of this static_assert
  static_assert(sizeof timestamp_ + sizeof type_code_ + sizeof server_id_ +
                        sizeof event_size_ + sizeof next_event_position_ +
                        sizeof flags_ ==
                    size_in_bytes,
                "mismatch in common_header::size_in_bytes");
  // make sure we did OK with data members reordering
  static_assert(sizeof *this == boost::alignment::align_up(
                                    size_in_bytes, alignof(decltype(*this))),
                "inefficient data member reordering in common_header");

  if (std::size(portion) != size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid event common header length");
  }

  auto remainder = portion;
  util::extract_fixed_int_from_byte_span(remainder, timestamp_);
  util::extract_fixed_int_from_byte_span(remainder, type_code_);
  util::extract_fixed_int_from_byte_span(remainder, server_id_);
  util::extract_fixed_int_from_byte_span(remainder, event_size_);
  util::extract_fixed_int_from_byte_span(remainder, next_event_position_);
  util::extract_fixed_int_from_byte_span(remainder, flags_);

  if (get_type_code_raw() >= util::enum_to_index(code_type::delimiter) ||
      to_string_view(get_type_code()).empty()) {
    util::exception_location().raise<std::logic_error>(
        "invalid event code in event header");
  }
  // TODO: check if flags are valid (all the bits have corresponding enum)
}

[[nodiscard]] std::string common_header::get_readable_timestamp() const {
  return boost::posix_time::to_simple_string(
      boost::posix_time::from_time_t(get_timestamp()));
}

[[nodiscard]] std::string_view
common_header::get_readable_type_code() const noexcept {
  return to_string_view(get_type_code());
}

[[nodiscard]] flag_set common_header::get_flags() const noexcept {
  return flag_set{get_flags_raw()};
}

[[nodiscard]] std::string common_header::get_readable_flags() const {
  return to_string(get_flags());
}

} // namespace binsrv::event
