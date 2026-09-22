// Copyright (c) 2023-2026 Percona and/or its affiliates.
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

#include "binsrv/authentication_config.hpp"

#include <stdexcept>
#include <string_view>

#include "util/exception_location_helpers.hpp"

namespace binsrv {

void authentication_config::validate() const {
  // The only client authentication plugin the PBS listener supports
  // today. Anything else is rejected up front so that a misconfigured
  // JSON cannot silently downgrade a session's auth negotiation.
  static constexpr std::string_view supported_plugin{"caching_sha2_password"};

  if (get<"user">().empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating replication source authentication config: "
        "user must not be empty");
  }
  if (get<"password">().empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating replication source authentication config: "
        "password must not be empty");
  }
  if (get<"plugin">() != supported_plugin) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating replication source authentication config: "
        "plugin must be \"caching_sha2_password\"");
  }
}

} // namespace binsrv
