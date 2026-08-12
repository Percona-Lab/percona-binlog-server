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

#ifndef BINSRV_KEYRING_RECORD_HPP
#define BINSRV_KEYRING_RECORD_HPP

#include "binsrv/keyring_record_fwd.hpp" // IWYU pragma: export

#include <string>

#include "util/hex_value.hpp"
#include "util/nv_tuple.hpp"

namespace binsrv {

struct [[nodiscard]] keyring_record
    : util::nv_tuple<
          // clang-format off
          util::nv<"id", std::string>,
          util::nv<"cipher", std::string>,
          util::nv<"data_hex", util::hex_value>
          // clang-format on
          > {
  [[nodiscard]] std::string get_description() const {
    std::string result;
    result += get<"id">();
    result += '(';
    result += get<"cipher">();
    result += ')';
    return result;
  }
  void validate() const;
};

} // namespace binsrv

#endif // BINSRV_KEYRING_RECORD_HPP
