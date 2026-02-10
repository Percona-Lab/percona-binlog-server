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

#include "binsrv/models/error_response.hpp"

#include <string>
#include <string_view>

#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>

#include "binsrv/models/response_status_type.hpp"

#include "util/nv_tuple_to_json.hpp"

namespace binsrv::models {

error_response::error_response(std::string_view message)
    : impl_{{expected_error_response_version},
            {response_status_type::error},
            {std::string{message}}} {}

[[nodiscard]] std::string error_response::str() const {
  boost::json::value json_value;
  util::nv_tuple_to_json(json_value, impl_);

  return boost::json::serialize(json_value);
}

} // namespace binsrv::models
