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

#include "binsrv/events/footer.hpp"

#include <iterator>
#include <ostream>
#include <stdexcept>

#include <boost/align/align_up.hpp>

#include "binsrv/events/footer_view.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::events {

footer::footer(const footer_view &view) : crc_(view.get_crc_raw()) {
  // TODO: initialize size_in_bytes directly based on the sum of fields
  // widths instead of this static_assert
  static_assert(sizeof crc_ == size_in_bytes,
                "mismatch in footer::size_in_bytes");
  // make sure we did OK with data members reordering
  static_assert(sizeof *this == boost::alignment::align_up(
                                    size_in_bytes, alignof(decltype(*this))),
                "inefficient data member reordering in footer");
}

footer::footer(util::const_byte_span portion) : footer{footer_view{portion}} {}

void footer::encode_to(util::byte_span &destination) const {
  if (std::size(destination) < calculate_encoded_size()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot encode footer");
  }
  const footer_updatable_view footer_uv{
      destination.subspan(0U, calculate_encoded_size())};
  footer_uv.set_crc_raw(get_crc_raw());
  destination = destination.subspan(calculate_encoded_size());
}

std::ostream &operator<<(std::ostream &output, const footer &obj) {
  return output << "crc: " << obj.get_readable_crc();
}

} // namespace binsrv::events
