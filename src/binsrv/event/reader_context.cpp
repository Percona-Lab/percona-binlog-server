#include "binsrv/event/reader_context.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "binsrv/event/checksum_algorithm_type.hpp"
#include "binsrv/event/code_type.hpp"
#include "binsrv/event/event.hpp"
#include "binsrv/event/protocol_traits.hpp"

#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

reader_context::reader_context()
    : checksum_algorithm_{checksum_algorithm_type::off} {}

void reader_context::process_event(const event &current_event) {
  // TODO: add some kind of state machine where the expected sequence of events
  //       is the following -
  //       (ROTATE(artificial) FORMAT_DESCRIPTION <ANY>* ROTATE)*
  if (current_event.get_common_header().get_type_code() ==
      code_type::format_description) {
    const auto &post_header{
        current_event.get_post_header<code_type::format_description>()};
    const auto &body{current_event.get_body<code_type::format_description>()};

    // check if FDE has expected binlog version number
    if (post_header.get_binlog_version_raw() != default_binlog_version) {
      util::exception_location().raise<std::logic_error>(
          "unexpected binlog version number in format description event");
    }

    // check if FDE has expected common header size
    if (post_header.get_common_header_length() !=
        default_common_header_length) {
      util::exception_location().raise<std::logic_error>(
          "unexpected common header length in format description event");
    }

    // check if the values from the post_header_lengths array are the same as
    // generic_post_header_impl<code_type::xxx>::size_in_bytes for known events

    // here we use a trick with immediately invoked lambda to initialize a
    // constexpr array which would have expected post header lengths for all
    // event codes based on generic_post_header<xxx>::size_in_bytes

    // we ignore the very first element in the code_type enum
    // (code_type::unknown) since the post header length for this value is
    // simply not included into FDE post header

    // therefore, the size of the array is default_number_of_events - 1
    using length_container =
        std::array<std::size_t, default_number_of_events - 1U>;
    static constexpr length_container expected_post_header_lengths{
        []<std::size_t... IndexPack>(
            std::index_sequence<IndexPack...>) -> length_container {
          return {generic_post_header<util::index_to_enum<code_type>(
              IndexPack + 1U)>::size_in_bytes...};
        }(std::make_index_sequence<default_number_of_events - 1U>{})};

    static_assert(
        std::tuple_size_v<std::remove_cvref_t<
                decltype(post_header.get_post_header_lengths_raw())>> ==
            std::tuple_size_v<decltype(expected_post_header_lengths)>,
        "mismatch in size between expected_header_lengths and "
        "post_header.get_post_header_lengths_raw()");
    const auto length_mismatch_result{std::ranges::mismatch(
        post_header.get_post_header_lengths_raw(), expected_post_header_lengths,
        [](post_header_length_container::value_type real,
           std::size_t expected) {
          return expected == unknown_post_header::size_in_bytes ||
                 static_cast<std::size_t>(real) == expected;
        })};
    if (length_mismatch_result.in2 != std::end(expected_post_header_lengths)) {
      const auto offset{static_cast<std::size_t>(
          std::distance(std::begin(expected_post_header_lengths),
                        length_mismatch_result.in2))};
      const std::string label{
          to_string_view(util::index_to_enum<code_type>(offset + 1U))};
      util::exception_location().raise<std::logic_error>(
          "mismatch in expected post header length in FDE for '" + label +
          "' event");
    }

    fde_processed_ = true;
    post_header_lengths_ = post_header.get_post_header_lengths_raw();
    checksum_algorithm_ = body.get_checksum_algorithm();
  }
  // TODO: add position counter and check if common_header.event_size /
  //       common_header. next_event_position match this counter
  // TODO: check if CRC32 checksum from the footer (if present) matches the
  //       calculated one
}

[[nodiscard]] checksum_algorithm_type
reader_context::get_current_checksum_algorithm() const noexcept {
  assert(has_fde_processed());
  return checksum_algorithm_;
}

[[nodiscard]] std::size_t
reader_context::get_current_post_header_length(code_type code) const noexcept {
  assert(has_fde_processed());
  return get_post_header_length_for_code(post_header_lengths_, code);
}

} // namespace binsrv::event
