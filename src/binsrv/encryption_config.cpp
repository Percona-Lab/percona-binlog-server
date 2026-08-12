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

#include "binsrv/encryption_config.hpp"

#include <stdexcept>

#include "binsrv/encryption_format_type.hpp"

#include "util/exception_location_helpers.hpp"

#include "opensslpp/cipher_context.hpp"
#include "opensslpp/cipher_mode_type.hpp"

namespace binsrv {

void encryption_config::validate() const {
  if (get<"format">() != encryption_format_type::generic) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating storage encryption config: unsupported format");
  }

  if (opensslpp::cipher_context::get_mode(get<"cipher">()) !=
      opensslpp::cipher_mode_type::ctr) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating storage encryption config: only CTR mode is "
        "supported for data encryption cipher");
  }
}

} // namespace binsrv
