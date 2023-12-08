#include "binsrv/event/format_description_post_header_impl.hpp"

#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

#include <boost/align/align_up.hpp>

#include <boost/date_time/posix_time/conversion.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>

#include "binsrv/event/code_type.hpp"

#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

generic_post_header_impl<code_type::format_description>::
    generic_post_header_impl(util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  /*
    https://github.com/mysql/mysql-server/blob/mysql-8.0.35/libbinlogevents/include/control_events.h#L286

    +=====================================+
    | event  | binlog_version   19 : 2    | = 4
    | data   +----------------------------+
    |        | server_version   21 : 50   |
    |        +----------------------------+
    |        | create_timestamp 71 : 4    |
    |        +----------------------------+
    |        | header_length    75 : 1    |
    |        +----------------------------+
    |        | post-header      76 : n    | = array of n bytes, one byte
    |        | lengths for all            |   per event type that the
    |        | event types                |   server knows about
    +=====================================+

  */

  static_assert(server_version_length ==
                    std::tuple_size_v<decltype(server_version_)>,
                "mismatch in "
                "generic_post_header_impl<code_type::format_description>::"
                "server_version_length");
  static_assert(number_of_events ==
                    util::enum_to_index(code_type::delimiter) - 1U,
                "mismatch in "
                "generic_post_header_impl<code_type::format_description>::"
                "number_of_events");

  // TODO: initialize size_in_bytes directly based on the sum of fields
  // widths instead of this static_assert
  static_assert(
      sizeof binlog_version_ + std::tuple_size_v<decltype(server_version_)> +
              sizeof create_timestamp_ + sizeof common_header_length_ +
              std::tuple_size_v<decltype(post_header_lengths_)> ==
          size_in_bytes,
      "mismatch in "
      "generic_post_header_impl<code_type::format_description>::size_in_bytes");
  // make sure we did OK with data members reordering
  static_assert(sizeof *this == boost::alignment::align_up(
                                    size_in_bytes, alignof(decltype(*this))),
                "inefficient data member reordering in "
                "generic_post_header_impl<code_type::format_description>");

  if (std::size(portion) != size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid format_description event post-header length");
  }

  auto remainder = portion;
  util::extract_fixed_int_from_byte_span(remainder, binlog_version_);
  util::extract_byte_array_from_byte_span(remainder, server_version_);
  util::extract_fixed_int_from_byte_span(remainder, create_timestamp_);
  util::extract_fixed_int_from_byte_span(remainder, common_header_length_);
  util::extract_byte_array_from_byte_span(remainder, post_header_lengths_);
}

[[nodiscard]] std::string_view
generic_post_header_impl<code_type::format_description>::get_server_version()
    const noexcept {
  // in case when every byte of the server version is significant
  // we cannot rely on std::string_view::string_view(const char*)
  // constructor as '\0' character will never be found
  auto result{util::as_string_view(server_version_)};
  auto position{result.find('\0')};
  if (position != std::string_view::npos) {
    result.remove_suffix(result.size() - position);
  }
  return result;
}

[[nodiscard]] std::string generic_post_header_impl<
    code_type::format_description>::get_readable_create_timestamp() const {
  return boost::posix_time::to_simple_string(
      boost::posix_time::from_time_t(get_create_timestamp()));
}

[[nodiscard]] std::size_t
generic_post_header_impl<code_type::format_description>::get_post_header_length(
    code_type code) const noexcept {
  // here the very first "unknown" code is not included in the array by the
  // spec
  const auto index{util::enum_to_index(code)};
  if (index == 0U || index >= util::enum_to_index(code_type::delimiter)) {
    return 0U;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  return static_cast<std::size_t>(post_header_lengths_[index - 1U]);
}

} // namespace binsrv::event
