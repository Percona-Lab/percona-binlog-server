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

#include "binsrv/gtids/tag.hpp"

#include <cctype>
#include <cstddef>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <string_view>

#include "binsrv/gtids/common_types.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/byte_span_inserters.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::gtids {

tag::tag(std::string_view name) {
  static constexpr char underscore{'_'};

  const auto name_size{std::size(name)};
  if (std::size(name) > tag_max_length) {
    util::exception_location().raise<std::invalid_argument>(
        "tag name is too long");
  }
  if (name_size == 0U) {
    return;
  }
  data_.resize(name_size);
  const char *name_it{std::data(name)};
  const char *name_en{name_it};
  std::advance(name_en, name_size);
  std::byte *data_it{std::data(data_)};
  auto current_ch{*name_it};
  if (current_ch != underscore && std::isalpha(current_ch) == 0) {
    util::exception_location().raise<std::invalid_argument>(
        "tag name must start with a letter or an underscore");
  }
  *data_it = static_cast<std::byte>(current_ch);
  std::advance(name_it, 1U);
  std::advance(data_it, 1U);
  for (; name_it != name_en;
       std::advance(name_it, 1U), std::advance(data_it, 1U)) {
    current_ch = *name_it;
    if (current_ch != underscore && std::isalnum(current_ch) == 0) {
      util::exception_location().raise<std::invalid_argument>(
          "tag name must includeonly alphanumeric characters or underscores");
    }
    *data_it = static_cast<std::byte>(current_ch);
  }
}

[[nodiscard]] std::size_t tag::calculate_encoded_size() const noexcept {
  // varlen bytes for tag size
  // 1 byte for each character in the tag
  const auto tag_size{get_size()};
  return util::calculate_varlen_int_size(tag_size) + tag_size;
}

void tag::encode_to(util::byte_span &destination) const {
  util::byte_span remainder{destination};
  if (!util::insert_varlen_int_to_byte_span_checked(remainder, get_size())) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot encode tag length");
  }
  if (!util::insert_byte_span_to_byte_span_checked(
          remainder, util::const_byte_span{data_})) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot encode tag data");
  }
  destination = remainder;
}

std::ostream &operator<<(std::ostream &output, const tag &obj) {
  return output << obj.get_name();
}

} // namespace binsrv::gtids
