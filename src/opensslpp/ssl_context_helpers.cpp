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

#include "opensslpp/ssl_context_helpers.hpp"

#include <cassert>
#include <string_view>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/types.h>

#include "opensslpp/core_error.hpp"

#include "util/exception_location_helpers.hpp"

namespace opensslpp {

void verify_ssl_ctx_private_key_matches_certificate(
    void *native_ssl_ctx, std::string_view error_prefix) {
  assert(native_ssl_ctx != nullptr);
  // A prior successful SSL_CTX_use_certificate_chain_file() or
  // SSL_CTX_use_PrivateKey_file() can leave advisory entries on the OpenSSL
  // error queue; drain them so the exception raised on mismatch reports the
  // real SSL_CTX_check_private_key() reason instead of leftover noise.
  ERR_clear_error();
  if (SSL_CTX_check_private_key(static_cast<SSL_CTX *>(native_ssl_ctx)) != 1) {
    util::exception_location().raise<core_error>(error_prefix);
  }
}

} // namespace opensslpp
