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
  // When the block is present at all, both paths must be non-empty; the
  // authenticator loads them together and a one-sided configuration would
  // fail deep inside opensslpp with a less actionable error.
  const auto &public_key{get<"rsa_public_key_path">()};
  const auto &private_key{get<"rsa_private_key_path">()};
  if (public_key.empty() || private_key.empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating pbs_listener config: "
        "rsa_public_key_path and rsa_private_key_path must both be "
        "non-empty when 'pbs_listener' is set");
  }
}

} // namespace binsrv
