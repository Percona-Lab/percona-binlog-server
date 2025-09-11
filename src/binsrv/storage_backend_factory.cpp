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

#include "binsrv/storage_backend_factory.hpp"

#include <cassert>
#include <memory>
#include <stdexcept>

#include <boost/url/parse.hpp>

#include "binsrv/basic_storage_backend_fwd.hpp"
#include "binsrv/filesystem_storage_backend.hpp"
#include "binsrv/s3_storage_backend.hpp"
#include "binsrv/storage_backend_type.hpp"
#include "binsrv/storage_config.hpp"

#include "util/exception_location_helpers.hpp"

namespace binsrv {

basic_storage_backend_ptr
storage_backend_factory::create(const storage_config &config) {
  const auto &backend_uri = config.get<"uri">();

  const auto uri_parse_result{boost::urls::parse_absolute_uri(backend_uri)};
  if (!uri_parse_result) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid storage backend URI");
  }

  const auto storage_backend = config.get<"backend">();

  switch (storage_backend) {
  case storage_backend_type::file:
    return std::make_unique<filesystem_storage_backend>(*uri_parse_result);
  case storage_backend_type::s3:
    return std::make_unique<s3_storage_backend>(*uri_parse_result);
  default:
    assert(false);
  }
  return {}; // should never get here
}

} // namespace binsrv
