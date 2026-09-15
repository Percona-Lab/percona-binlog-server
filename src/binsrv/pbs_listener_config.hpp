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

#ifndef BINSRV_PBS_LISTENER_CONFIG_HPP
#define BINSRV_PBS_LISTENER_CONFIG_HPP

#include "binsrv/pbs_listener_config_fwd.hpp" // IWYU pragma: export

#include <string>

#include "util/nv_tuple.hpp"

namespace binsrv {

// Settings for the MySQL-protocol listener the Binlog Server exposes to
// downstream clients when running in the 'pull' operation (the source-side
// half of the replication graph). The whole block is optional in
// main_config.
//
// The block carries two independent pairs of options, each validated on
// its own (both fields set together, or both left empty):
//
//   * 'rsa_public_key_path' / 'rsa_private_key_path' - server-side RSA
//     key pair used by the caching_sha2_password authenticator to serve
//     --get-server-public-key and to RSA-OAEP-decrypt password ciphertext
//     (PBS-33). When empty, any caching_sha2_password full-auth attempt
//     (0x04) fails per-session - matching the authenticator's own "both
//     empty is OK" acceptance at construction. See
//     minimysql::caching_sha2_password_authenticator.
//
//   * 'ssl_cert_path' / 'ssl_key_path' - server-side TLS cert / key pair
//     for the optional TLS listener (PBS-31). When set, the listener
//     advertises CLIENT_SSL in its greeting and honours
//     Protocol::SSLRequest by upgrading the transport to TLS; when empty,
//     the listener runs in plaintext-only mode. See
//     minimysql::ssl_acceptor_context.
struct [[nodiscard]] pbs_listener_config
    : util::nv_tuple<
          // clang-format off
                                               util::nv<"rsa_public_key_path" , std::string>,
                                               util::nv<"rsa_private_key_path", std::string>,
                                               util::nv<"ssl_cert_path"       , std::string>,
                                               util::nv<"ssl_key_path"        , std::string>
          // clang-format on
          > {
  void validate() const;
};

} // namespace binsrv

#endif // BINSRV_PBS_LISTENER_CONFIG_HPP
