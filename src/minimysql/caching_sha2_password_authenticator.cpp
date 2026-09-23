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

#include "minimysql/caching_sha2_password_authenticator.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>

#include "opensslpp/digest_context.hpp"

namespace minimysql {

std::string caching_sha2_password_authenticator::scramble(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view password, std::string_view salt) {
  // this is how client calculates client_auth_data for caching_sha2_password
  // plugin: SHA256(password) XOR SHA256(SHA256(SHA256(password)),
  // server_auth_data)

  // server, provided that it knows original password and server_auth_data
  // (salt), can verify client_auth_data by calculating the same way and
  // comparing the result with client_auth_data
  const std::string digest_name{"SHA256"};

  // calculating hashed password
  auto result{opensslpp::digest_context::calculate(digest_name, password)};

  // calculating double-hashed password
  const auto double_hashed_password{
      opensslpp::digest_context::calculate(digest_name, result)};

  // calculating salted triple-hashed password
  opensslpp::digest_context ctx{digest_name};
  ctx.update(double_hashed_password);
  ctx.update(salt);
  const auto salted_triple_hashed_password{ctx.finalize()};

  assert(std::size(result) == std::size(salted_triple_hashed_password));
  std::ranges::transform(result, salted_triple_hashed_password,
                         std::begin(result), std::bit_xor<std::uint8_t>{});
  return result;
}

} // namespace minimysql
