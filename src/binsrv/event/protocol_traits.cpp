// Copyright (c) 2023-2024 Percona and/or its affiliates.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 2.0,
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License, version 2.0, for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

#include "binsrv/event/protocol_traits.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <iterator>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <string>

#include <boost/io/ios_state.hpp>

#include "binsrv/event/code_type.hpp"

#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

[[nodiscard]] std::size_t
get_post_header_length_for_code(std::uint32_t encoded_server_version,
                                const post_header_length_container &storage,
                                code_type code) noexcept {
  static_assert(max_number_of_events ==
                    util::enum_to_index(code_type::delimiter),
                "mismatch between number_of_events and code_type enum");

  // here the very first "unknown" code is not included in the array by the
  // spec
  const auto index{util::enum_to_index(code)};
  if (index == 0U || index >= get_number_of_events(encoded_server_version)) {
    return unspecified_post_header_length;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  const auto encoded_length{storage[index - 1U]};
  return encoded_length == static_cast<encoded_post_header_length_type>(
                               unspecified_post_header_length)
             ? unspecified_post_header_length
             : static_cast<std::size_t>(encoded_length);
}

std::ostream &
print_post_header_lengths(std::ostream &output,
                          std::uint32_t encoded_server_version,
                          const post_header_length_container &obj) {
  const boost::io::ios_flags_saver saver(output);
  const auto max_index = get_number_of_events(encoded_server_version);
  // starting from 1 here as the very first "unknown" code is not included
  // in this array
  for (std::size_t index{1U}; index < max_index; ++index) {
    if (index != 1U) {
      output << '\n';
    }
    const auto current_code{util::index_to_enum<code_type>(index)};
    const auto label{to_string_view(current_code)};

    static constexpr std::size_t alignment_width{19U};
    if (label.empty()) {
      std::string generated_label{"<unused code "};
      generated_label += std::to_string(index);
      generated_label += '>';
      output << std::left << std::setw(alignment_width) << generated_label;
    } else {
      output << std::left << std::setw(alignment_width) << label;
    }
    output << " => "
           << get_post_header_length_for_code(encoded_server_version, obj,
                                              current_code);
  }
  return output;
}

void validate_post_header_lengths(
    std::uint32_t encoded_server_version,
    const post_header_length_container &runtime,
    const post_header_length_container &hardcoded) {
  const auto number_of_events{get_number_of_events(encoded_server_version)};
  const auto length_mismatch_result{std::ranges::mismatch(
      runtime | std::ranges::views::take(number_of_events),
      hardcoded | std::ranges::views::take(number_of_events),
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
