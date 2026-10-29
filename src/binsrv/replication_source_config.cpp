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

#include "binsrv/replication_source_config.hpp"

#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string_view>

#include "util/exception_location_helpers.hpp"

namespace binsrv {

namespace {

// Checks the canonical 8-4-4-4-12 hex shape with '-' separators (36 chars),
// matching @@GLOBAL.SERVER_UUID.
[[nodiscard]] bool is_canonical_uuid(std::string_view value) noexcept {
  static constexpr std::size_t uuid_length{36U};
  if (value.size() != uuid_length) {
    return false;
  }
  for (std::size_t index{0U}; index < uuid_length; ++index) {
    const char character{value[index]};
    const bool separator_slot{index == 8U || index == 13U || index == 18U ||
                              index == 23U};
    if (separator_slot) {
      if (character != '-') {
        return false;
      }
    } else {
      if (std::isxdigit(static_cast<unsigned char>(character)) == 0) {
        return false;
      }
    }
  }
  return true;
}

} // anonymous namespace

void replication_source_config::validate() const {
  if (get<"port">() == 0U) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating replication source config: "
        "port must be greater than 0");
  }
  if (!is_canonical_uuid(get<"server_uuid">())) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating replication source config: "
        "server_uuid must be a canonical 8-4-4-4-12 hex UUID");
  }
  if (get<"read_timeout">() == 0U) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating replication source config: "
        "read_timeout must be greater than 0");
  }
  if (get<"write_timeout">() == 0U) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating replication source config: "
        "write_timeout must be greater than 0");
  }
  const auto &optional_ssl{get<"ssl">()};
  if (get<"require_secure_transport">()) {
    // in the case when secure transport is required, the 'ssl' section
    // must be present
    if (!optional_ssl.has_value()) {
      util::exception_location().raise<std::invalid_argument>(
          "error validating replication source config: "
          "ssl section must be present when secure transport is required");
    }
  } else {
    // in the case when secure transport is not required, auth_rsa_private_key
    // must be present because there will be no way to perform authentication
    // otherwise
    const auto &optional_auth_rsa_private_key{get<"auth_rsa_private_key">()};
    if (optional_auth_rsa_private_key.has_value() &&
        optional_auth_rsa_private_key->empty()) {
      util::exception_location().raise<std::invalid_argument>(
          "error validating replication source config: "
          "auth_rsa_private_key must not be empty when secure transport is not "
          "required");
    }
  }
  // if the 'ssl' section is present, it must be valid
  if (optional_ssl.has_value()) {
    optional_ssl->validate();
  }
  get<"authentication">().validate();
}

} // namespace binsrv
