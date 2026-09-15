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

#include "binsrv/pbs_listener_config.hpp"

#include <stdexcept>

#include "util/exception_location_helpers.hpp"

namespace binsrv {

void pbs_listener_config::validate() const {
  // The two field pairs are validated independently. Each pair uses the
  // "both set or both empty" invariant - a one-sided configuration is a
  // mis-configuration that would fail deeper in the load path (opensslpp
  // for RSA, boost::asio::ssl::context for TLS) with a less actionable
  // error. Both-empty for a pair means "not configured", which is fine:
  // the caching_sha2_password authenticator accepts empty RSA paths (any
  // 0x04 full-auth attempt then fails per-session) and the network layer
  // accepts empty SSL paths (listener stays plaintext-only).
  const auto &rsa_public_key{get<"rsa_public_key_path">()};
  const auto &rsa_private_key{get<"rsa_private_key_path">()};
  if (rsa_public_key.empty() != rsa_private_key.empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating pbs_listener config: "
        "rsa_public_key_path and rsa_private_key_path must both be set "
        "or both be empty");
  }

  const auto &ssl_cert{get<"ssl_cert_path">()};
  const auto &ssl_key{get<"ssl_key_path">()};
  if (ssl_cert.empty() != ssl_key.empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating pbs_listener config: "
        "ssl_cert_path and ssl_key_path must both be set or both be empty");
  }
}

} // namespace binsrv
