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

#include "binsrv/event/previous_gtids_log_body_impl.hpp"

#include <cstddef>
#include <iterator>
#include <ostream>
#include <span>
#include <string>

#include <boost/lexical_cast.hpp>

#include "binsrv/event/code_type.hpp"

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"

namespace binsrv::event {

generic_body_impl<code_type::previous_gtids_log>::generic_body_impl(
    util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  auto remainder = portion;
  const std::size_t gtids_size{std::size(remainder)};
  gtids_.resize(gtids_size);
  const std::span<gtids::gtid_set_storage::value_type> gtids_range{gtids_};
  util::extract_byte_span_from_byte_span(remainder, gtids_range);
}

[[nodiscard]] gtids::gtid_set
generic_body_impl<code_type::previous_gtids_log>::get_gtids() const {
  return gtids::gtid_set{gtids_};
}

[[nodiscard]] std::string
generic_body_impl<code_type::previous_gtids_log>::get_readable_gtids() const {
  return boost::lexical_cast<std::string>(get_gtids());
}

std::ostream &
operator<<(std::ostream &output,
           const generic_body_impl<code_type::previous_gtids_log> &obj) {
  return output << "gtid_set: " << obj.get_readable_gtids();
}

} // namespace binsrv::event
