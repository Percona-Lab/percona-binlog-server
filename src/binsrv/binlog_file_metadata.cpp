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

#include "binsrv/binlog_file_metadata.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>

#include "util/exception_location_helpers.hpp"
#include "util/nv_tuple_from_json.hpp"
#include "util/nv_tuple_to_json.hpp"

namespace binsrv {

binlog_file_metadata::binlog_file_metadata()
    : impl_{{expected_binlog_file_metadata_version}, {}, {}, {}, {}, {}} {}

binlog_file_metadata::binlog_file_metadata(std::string_view data) : impl_{} {
  auto json_value = boost::json::parse(data);
  util::nv_tuple_from_json(json_value, impl_);

  validate();
}

[[nodiscard]] std::string binlog_file_metadata::str() const {
  boost::json::value json_value;
  util::nv_tuple_to_json(json_value, impl_);

  return boost::json::serialize(json_value);
}

void binlog_file_metadata::validate() const {
  if (root().get<"version">() != expected_binlog_file_metadata_version) {
    util::exception_location().raise<std::invalid_argument>(
        "unsupported binlog file metadata version");
  }
}

} // namespace binsrv
