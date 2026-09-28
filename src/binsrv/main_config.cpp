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

#include "binsrv/main_config.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/json/parse.hpp>

// Needed for encryption_format's operator <<
#include "binsrv/encryption_format_type.hpp" // IWYU pragma: keep
// Needed for log_severity's operator <<
#include "binsrv/log_severity.hpp" // IWYU pragma: keep
// Needed for replication_mode_type's operator <<
#include "binsrv/replication_mode_type.hpp" // IWYU pragma: keep
// Needed for storage_backend_type's operator <<
#include "binsrv/storage_backend_type.hpp" // IWYU pragma: keep

// Needed for ssl_mode_type's operator <<
#include "easymysql/ssl_mode_type.hpp" // IWYU pragma: keep

#include "util/exception_location_helpers.hpp"
#include "util/file_operations_helpers.hpp"
#include "util/nv_tuple_from_json.hpp"

namespace binsrv {

main_config::main_config(std::string_view file_name) {
  static constexpr std::size_t max_file_size{1048576U};

  const auto file_content =
      util::read_file_content("configuration file", file_name, max_file_size);
  if (file_content.empty()) {
    util::exception_location().raise<std::out_of_range>(
        "configuration file is empty");
  }

  auto json_value = boost::json::parse(file_content);
  util::nv_tuple_from_json(json_value, impl_);

  validate();
}

void main_config::validate() const {
  root().get<"connection">().validate();
  root().get<"storage">().validate();
  root().get<"replication">().validate();
  root().get<"replication_source">().validate();
}

} // namespace binsrv
