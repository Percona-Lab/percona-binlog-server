#include "binsrv/event/protocol_traits.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>

#include "binsrv/event/code_type.hpp"

#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

[[nodiscard]] std::size_t
get_post_header_length_for_code(const post_header_length_container &storage,
                                code_type code) noexcept {
  static_assert(default_number_of_events ==
                    util::enum_to_index(code_type::delimiter),
                "mismatch between number_of_events and code_type enum");

  // here the very first "unknown" code is not included in the array by the
  // spec
  const auto index{util::enum_to_index(code)};
  if (index == 0U || index >= default_number_of_events) {
    return unspecified_post_header_length;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  const auto encoded_length{storage[index - 1U]};
  return encoded_length == static_cast<encoded_post_header_length_type>(
                               unspecified_post_header_length)
             ? unspecified_post_header_length
             : static_cast<std::size_t>(encoded_length);
}

void validate_post_header_lengths(
    const post_header_length_container &runtime,
    const post_header_length_container &hardcoded) {
  const auto length_mismatch_result{std::ranges::mismatch(
      runtime, hardcoded,
      [](encoded_post_header_length_type real,
         encoded_post_header_length_type expected) {
        return expected == static_cast<encoded_post_header_length_type>(
                               unspecified_post_header_length) ||
               real == expected;
      })};
  if (length_mismatch_result.in2 != std::end(hardcoded)) {
    const auto offset{static_cast<std::size_t>(
        std::distance(std::begin(hardcoded), length_mismatch_result.in2))};
    const std::string label{
        to_string_view(util::index_to_enum<code_type>(offset + 1U))};
    util::exception_location().raise<std::logic_error>(
        "mismatch in expected post header length for '" + label + "' event");
  }
}

} // namespace binsrv::event
