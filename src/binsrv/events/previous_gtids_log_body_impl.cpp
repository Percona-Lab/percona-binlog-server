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

#include "binsrv/events/previous_gtids_log_body_impl.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>

#include <boost/lexical_cast.hpp>

#include "binsrv/events/code_type.hpp"

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::events {

generic_body_impl<code_type::previous_gtids_log>::generic_body_impl(
    const gtids::gtid_set &gtids)
    : gtids_(gtids.calculate_encoded_size()) {
  util::byte_span gtids_span{gtids_};
  gtids.encode_to(gtids_span);
}

generic_body_impl<code_type::previous_gtids_log>::generic_body_impl(
    util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  auto remainder = portion;
  const std::size_t gtids_size{std::size(remainder)};
  gtids_.resize(gtids_size);
  const util::byte_span gtids_span{gtids_};
  util::extract_byte_span_from_byte_span(remainder, gtids_span);
}

[[nodiscard]] gtids::gtid_set
generic_body_impl<code_type::previous_gtids_log>::get_gtids() const {
  return gtids::gtid_set{gtids_};
}

[[nodiscard]] std::string
generic_body_impl<code_type::previous_gtids_log>::get_readable_gtids() const {
  return boost::lexical_cast<std::string>(get_gtids());
}

void generic_body_impl<code_type::previous_gtids_log>::encode_to(
    util::byte_span &destination) const {
  if (std::size(destination) < calculate_encoded_size()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot encode previous gtids log event body");
  }
  std::ranges::copy(gtids_, std::begin(destination));
  destination = destination.subspan(calculate_encoded_size());
}

std::ostream &
operator<<(std::ostream &output,
           const generic_body_impl<code_type::previous_gtids_log> &obj) {
  return output << "gtid_set: " << obj.get_readable_gtids();
}

} // namespace binsrv::events
