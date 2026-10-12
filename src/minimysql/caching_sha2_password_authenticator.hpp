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

#include "opensslpp/digest_context_fwd.hpp"
#include "opensslpp/evp_pkey_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace minimysql {

class caching_sha2_password_authenticator {
public:
  static constexpr std::string_view plugin_name{"caching_sha2_password"};

  [[nodiscard]] static opensslpp::digest_storage
  scramble(util::const_byte_span password, util::const_byte_span salt);

  // performs the server-side part of the caching_sha2_password "full
  // authentication" over an insecure connection: 'encrypted_password' is
  // expected to be the RSA (with OAEP padding) encrypted null-terminated
  // password XOR-ed with 'nonce' (repeated cyclically), which the client
  // sends in response to the "perform full authentication" packet;
  // returns the plain text password (without the trailing null terminator)
  [[nodiscard]] static std::string
  decrypt_password(util::const_byte_span encrypted_password,
                   util::const_byte_span nonce,
                   const opensslpp::evp_pkey &private_key);
};

} // namespace minimysql

#endif // MINIMYSQL_CACHING_SHA2_PASSWORD_AUTHENTICATOR_HPP
