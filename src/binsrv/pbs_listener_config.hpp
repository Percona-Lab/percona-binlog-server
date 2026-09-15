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
// main_config; when omitted the listener has no server-side RSA key pair
// and any caching_sha2_password full-authentication attempt (0x04) will
// fail per-session - matching the authenticator's own "both empty is OK"
// acceptance at construction.
//
// When the block IS present, both 'rsa_public_key_path' and
// 'rsa_private_key_path' must be non-empty and readable PEM files - the
// authenticator loads them to serve --get-server-public-key and to
// RSA-OAEP-decrypt password ciphertext (see PBS-33 and
// minimysql::caching_sha2_password_authenticator).
struct [[nodiscard]] pbs_listener_config
    : util::nv_tuple<
          // clang-format off
                                               util::nv<"rsa_public_key_path" , std::string>,
                                               util::nv<"rsa_private_key_path", std::string>
          // clang-format on
          > {
  void validate() const;
};

} // namespace binsrv

#endif // BINSRV_PBS_LISTENER_CONFIG_HPP
