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

#include "binsrv/storage_metadata.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>

// Needed for replication_mode_type's operator <<
#include "binsrv/replication_mode_type.hpp" // IWYU pragma: keep

#include "util/nv_tuple_from_json.hpp"
#include "util/nv_tuple_to_json.hpp"

namespace binsrv {

storage_metadata::storage_metadata()
    : impl_{{expected_storage_metadata_version}, {}} {}

storage_metadata::storage_metadata(std::string_view data) : impl_{} {
  auto json_value = boost::json::parse(data);
  util::nv_tuple_from_json(json_value, impl_);

  validate();
}

[[nodiscard]] std::string storage_metadata::str() const {
  boost::json::value json_value;
  util::nv_tuple_to_json(json_value, impl_);

  return boost::json::serialize(json_value);
}

void storage_metadata::validate() const {
  if (root().get<"version">() != expected_storage_metadata_version) {
    util::exception_location().raise<std::invalid_argument>(
        "unsupported storage metadata version");
  }
}

} // namespace binsrv
