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
#include <cstddef>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

// needed for 'digest_storage'
#include <boost/container/static_vector.hpp> // IWYU pragma: keep

#include "opensslpp/digest_context.hpp"
#include "opensslpp/encryption_padding.hpp"
#include "opensslpp/evp_pkey.hpp"

#include "util/byte_span.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace minimysql {

[[nodiscard]] opensslpp::digest_storage
caching_sha2_password_authenticator::scramble(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    util::const_byte_span password, util::const_byte_span salt) {
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

  opensslpp::digest_storage salted_triple_hashed_password(
      ctx.get_digest_size_in_bytes());
  ctx.finalize(salted_triple_hashed_password);

  assert(std::size(result) == std::size(salted_triple_hashed_password));
  std::ranges::transform(result, salted_triple_hashed_password,
                         std::begin(result), std::bit_xor<std::byte>{});
  return result;
}

[[nodiscard]] std::string caching_sha2_password_authenticator::decrypt_password(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    util::const_byte_span encrypted_password, util::const_byte_span nonce,
    const opensslpp::evp_pkey &private_key) {
  assert(!private_key.is_empty());
  // this is how client prepares the encrypted password for
  // caching_sha2_password plugin full authentication over an insecure
  // connection (see 'xor_string()' and 'encrypt_RSA_public_key()' in MySQL
  // Server sources): RSA_OAEP_ENCRYPT(public_key, (password + '\0') XOR nonce),
  // where nonce is repeated cyclically if it is shorter than the password

  // server, provided that it knows the private key and the nonce, can
  // recover the plain text password by reversing these operations
  if (std::empty(nonce)) {
    util::exception_location().raise<std::invalid_argument>(
        "nonce for caching_sha2_password decryption must not be empty");
  }

  // evp_key_decrypt() also ensures that the size of 'encrypted_password'
  // matches the size of the key (just like MySQL Server does)
  util::dynamic_byte_buffer decrypted{
      opensslpp::evp_key_decrypt(encrypted_password, private_key,
                                 opensslpp::encryption_padding::pkcs1_oaep)};

  const auto nonce_size{std::size(nonce)};
  for (std::size_t index{0UZ}; index < std::size(decrypted); ++index) {
    decrypted[index] ^= nonce[index % nonce_size];
  }

  // MySQL Server determines the password length via strlen(), so everything
  // starting from the first null byte is ignored here as well
  const auto terminator_it{std::ranges::find(decrypted, std::byte{})};
  if (terminator_it == std::end(decrypted)) {
    util::exception_location().raise<std::invalid_argument>(
        "decrypted caching_sha2_password password is not null-terminated");
  }

  return std::string{util::as_string_view(
      util::const_byte_span{std::begin(decrypted), terminator_it})};
}

} // namespace minimysql
