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

#ifndef MINIMYSQL_CACHING_SHA2_PASSWORD_AUTHENTICATOR_HPP
#define MINIMYSQL_CACHING_SHA2_PASSWORD_AUTHENTICATOR_HPP

#include <string>
#include <string_view>

namespace minimysql {

class caching_sha2_password_authenticator {
public:
  static constexpr std::string_view plugin_name{"caching_sha2_password"};

  static std::string scramble(std::string_view password, std::string_view salt);
};

} // namespace minimysql

#endif // MINIMYSQL_CACHING_SHA2_PASSWORD_AUTHENTICATOR_HPP
