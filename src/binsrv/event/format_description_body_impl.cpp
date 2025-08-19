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

#include "binsrv/event/format_description_body_impl.hpp"

#include <cassert>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <string_view>

#include <boost/align/align_up.hpp>

#include "binsrv/event/checksum_algorithm_type.hpp"
#include "binsrv/event/code_type.hpp"

#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

generic_body_impl<code_type::format_description>::generic_body_impl(
    util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  // TODO: initialize size_in_bytes directly based on the sum of fields
  // widths instead of this static_assert
  static_assert(sizeof checksum_algorithm_ == size_in_bytes,
                "mismatch in format_description_event_body::size_in_bytes");
  // make sure we did OK with data members reordering
  static_assert(
      sizeof *this ==
          boost::alignment::align_up(size_in_bytes, alignof(decltype(*this))),
      "inefficient data member reordering in format_description_event_body");

  if (std::size(portion) != size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid format_description event body length");
  }

  auto remainder = portion;
  util::extract_fixed_int_from_byte_span(remainder, checksum_algorithm_);

  if (get_checksum_algorithm_raw() >=
      util::enum_to_index(checksum_algorithm_type::delimiter)) {
    util::exception_location().raise<std::logic_error>(
        "invalid checksum algorithm in format_description event body");
  }
}

[[nodiscard]] std::string_view generic_body_impl<
    code_type::format_description>::get_readable_checksum_algorithm()
    const noexcept {
  return to_string_view(get_checksum_algorithm());
}

[[nodiscard]] bool
generic_body_impl<code_type::format_description>::has_checksum_algorithm()
    const noexcept {
  const auto checksum_algorithm{get_checksum_algorithm()};
  assert(checksum_algorithm == checksum_algorithm_type::off ||
         checksum_algorithm == checksum_algorithm_type::crc32);
  return checksum_algorithm != checksum_algorithm_type::off;
}

std::ostream &
operator<<(std::ostream &output,
           const generic_body_impl<code_type::format_description> &obj) {
  return output << "checksum algorithm: "
                << obj.get_readable_checksum_algorithm();
}

} // namespace binsrv::event
