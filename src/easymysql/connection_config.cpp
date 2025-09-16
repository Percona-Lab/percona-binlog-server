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

#include "easymysql/connection_config.hpp"

#include <stdexcept>
#include <string>

#include "util/exception_location_helpers.hpp"

namespace easymysql {

std::string connection_config::get_connection_string() const {
  std::string res{};
  res += get<"user">();
  res += '@';
  if (has_dns_srv_name()) {
    const auto &opt_dns_srv_name{get<"dns_srv_name">()};
    res += "[DNS SRV: ";
    res += opt_dns_srv_name.value_or("<unspecified>");
    res += ']';

  } else {
    const auto &opt_host{get<"host">()};
    res += opt_host.value_or("<unspecified host>");
    res += ':';
    const auto &opt_port{get<"port">()};
    res += (opt_port.has_value() ? std::to_string(opt_port.value())
                                 : "<unspecified port>");
  }
  res += (has_password() ? " (password ***hidden***)" : " (no password)");
  return res;
}

void connection_config::validate() const {
  const auto has_dns_srv_name{get<"dns_srv_name">().has_value()};
  const auto has_host{get<"host">().has_value()};
  const auto has_port{get<"port">().has_value()};

  const bool valid{(has_dns_srv_name && !has_host && !has_port) ||
                   (!has_dns_srv_name && has_host && has_port)};
  if (!valid) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating connection config: "
        "either dns_srv_name or both host and port must be specified");
  }
}

} // namespace easymysql
