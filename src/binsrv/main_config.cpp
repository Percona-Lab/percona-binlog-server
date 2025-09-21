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
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <boost/json/parse.hpp>
#include <boost/json/src.hpp> // IWYU pragma: keep

// Needed for log_severity's operator <<
#include "binsrv/log_severity.hpp" // IWYU pragma: keep
#include "binsrv/size_unit_fwd.hpp"
// Needed for storage_backend_type's operator <<
#include "binsrv/storage_backend_type.hpp" // IWYU pragma: keep
#include "binsrv/time_unit_fwd.hpp"

// Needed for ssl_mode_type's operator <<
#include "easymysql/ssl_mode_type.hpp" // IWYU pragma: keep

#include "util/exception_location_helpers.hpp"
#include "util/nv_tuple_from_json.hpp"
#include "util/nv_tuple_fwd.hpp"

// As this file is the only place where we call util::nv_tuple_from_json(),
// which in turns calls tag_invoke() from the boost::json, we give the
// serialization/unserialization hints for this library here (instead of
// polluting each individual class definition).
template <>
struct util::is_string_convertable<binsrv::size_unit> : std::true_type {};
template <>
struct util::is_string_convertable<binsrv::time_unit> : std::true_type {};
template <>
struct util::is_string_convertable<binsrv::log_severity> : std::true_type {};
template <>
struct util::is_string_convertable<binsrv::storage_backend_type>
    : std::true_type {};
template <>
struct util::is_string_convertable<easymysql::ssl_mode_type> : std::true_type {
};

namespace binsrv {

main_config::main_config(std::string_view file_name) {
  static constexpr std::size_t max_file_size{1048576U};

  const std::filesystem::path file_path{file_name};
  std::ifstream ifs{file_path};
  if (!ifs.is_open()) {
    util::exception_location().raise<std::runtime_error>(
        "cannot open configuration file");
  }
  auto file_size = std::filesystem::file_size(file_path);
  if (file_size == 0ULL) {
    util::exception_location().raise<std::out_of_range>(
        "configuration file is empty");
  }
  if (file_size > max_file_size) {
    util::exception_location().raise<std::out_of_range>(
        "configuration file is too large");
  }

  std::string file_content(file_size, 'x');
  if (!ifs.read(std::data(file_content),
                static_cast<std::streamoff>(file_size))) {
    util::exception_location().raise<std::runtime_error>(
        "cannot read configuration file content");
  }

  auto json_value = boost::json::parse(file_content);
  util::nv_tuple_from_json(json_value, impl_);

  validate();
}

void main_config::validate() const { root().get<"connection">().validate(); }

} // namespace binsrv
