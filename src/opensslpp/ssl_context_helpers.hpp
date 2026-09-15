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

#ifndef OPENSSLPP_SSL_CONTEXT_HELPERS_HPP
#define OPENSSLPP_SSL_CONTEXT_HELPERS_HPP

#include <string_view>

namespace opensslpp {

// Verifies that the private key currently loaded in the OpenSSL SSL_CTX
// pointed to by 'native_ssl_ctx' matches the certificate loaded in the same
// context (OpenSSL's SSL_CTX_check_private_key). The OpenSSL error queue is
// cleared before the call so residual entries from a prior successful load
// (SSL_CTX_use_certificate_chain_file / SSL_CTX_use_PrivateKey_file may leave
// advisory reasons behind) do not leak into the exception raised on
// mismatch. Throws opensslpp::core_error prefixed with 'error_prefix' on
// mismatch; the exception message reads as
// "<error_prefix>: <lib>::<reason>".
//
// 'native_ssl_ctx' must be a non-null pointer returned by SSL_CTX_new(); the
// void * type is deliberate so this header can stay free of any OpenSSL
// include (matches the rest of the opensslpp API). Callers using
// Boost.Asio's ssl::context pass its native_handle().
void verify_ssl_ctx_private_key_matches_certificate(
    void *native_ssl_ctx, std::string_view error_prefix = {});

} // namespace opensslpp

#endif // OPENSSLPP_SSL_CONTEXT_HELPERS_HPP
