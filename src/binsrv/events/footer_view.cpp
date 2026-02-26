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

#include "binsrv/events/footer_view.hpp"

#include <cstdint>
#include <iomanip>
#include <ios>
#include <iterator>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "util/byte_span_extractors.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/byte_span_inserters.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::events {

footer_view_base::footer_view_base(util::byte_span portion)
    : portion_{portion} {
  if (std::size(portion) != size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid event footer length");
  }
}

[[nodiscard]] std::string
footer_view_base::get_readable_crc_from_raw(std::uint32_t crc) {
  std::ostringstream oss;
  oss << std::hex << std::showbase << std::setfill('0')
      << std::setw(sizeof(crc) * 2) << crc;
  return oss.str();
}

[[nodiscard]] std::uint32_t footer_view_base::get_crc_raw() const noexcept {
  std::uint32_t crc{};
  util::const_byte_span remainder{portion_.subspan(crc_offset, sizeof crc)};
  util::extract_fixed_int_from_byte_span(remainder, crc);
  return crc;
}

void footer_view_base::set_crc_raw(std::uint32_t crc) const noexcept {
  util::byte_span remainder{portion_.subspan(crc_offset, sizeof crc)};
  util::insert_fixed_int_to_byte_span(remainder, crc);
}

std::ostream &operator<<(std::ostream &output, const footer_view &obj) {
  return output << "crc: " << obj.get_readable_crc();
}

} // namespace binsrv::events
