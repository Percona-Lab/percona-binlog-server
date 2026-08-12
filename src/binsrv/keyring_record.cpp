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

#include "binsrv/keyring_record.hpp"

#include <stdexcept>
#include <string>

#include "opensslpp/cipher_context.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

void keyring_record::validate() const {
  const auto &cipher_name = get<"cipher">();

  const auto &key_id = get<"id">();
  if (!opensslpp::cipher_context::is_cipher_name_supported(cipher_name)) {
    util::exception_location().raise<std::invalid_argument>(
        "unsupported cipher in keyring record: '" + key_id + "'");
  }

  if (get<"data_hex">().get_size() !=
      opensslpp::cipher_context::get_key_size_in_bytes(cipher_name)) {
    util::exception_location().raise<std::invalid_argument>(
        "key data length mismatch in keyring record '" + key_id + "'");
  }
}

} // namespace binsrv
