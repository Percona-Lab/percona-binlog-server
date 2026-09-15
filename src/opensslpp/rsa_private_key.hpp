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

#ifndef OPENSSLPP_RSA_PRIVATE_KEY_HPP
#define OPENSSLPP_RSA_PRIVATE_KEY_HPP

#include "opensslpp/rsa_private_key_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "util/byte_span_fwd.hpp"

namespace opensslpp {

// Thin RAII wrapper around an OpenSSL EVP_PKEY holding an RSA private key
// loaded from a PEM-encoded buffer. Exposes the primitives needed by the
// caching_sha2_password full-authentication path: the cipher output length
// (== RSA modulus size in bytes) and RSA-OAEP decryption.
class rsa_private_key {
public:
  rsa_private_key() noexcept = default;
  // Loads a PEM-encoded RSA private key from an in-memory buffer.
  // 'pem' must reference a complete, self-contained PEM block; anything
  // OpenSSL's PEM_read_bio_PrivateKey() accepts is accepted here.
  explicit rsa_private_key(std::string_view pem);
  ~rsa_private_key() noexcept = default;

  rsa_private_key(const rsa_private_key &obj) = delete;
  rsa_private_key(rsa_private_key &&obj) noexcept = default;

  rsa_private_key &operator=(const rsa_private_key &obj) = delete;
  rsa_private_key &operator=(rsa_private_key &&obj) noexcept = default;

  void swap(rsa_private_key &obj) noexcept { impl_.swap(obj.impl_); }

  [[nodiscard]] bool is_empty() const noexcept { return !impl_; }

  // RSA cipher output length in bytes (equals the modulus size, i.e. the
  // ciphertext length OAEP will accept and the maximum plaintext length it
  // can return).
  [[nodiscard]] std::size_t get_cipher_length_in_bytes() const noexcept;

  // Decrypts 'ciphertext' using PKCS#1 v2 OAEP padding (OpenSSL default MGF
  // and hash parameters, i.e. SHA-1 for both). 'ciphertext' must be exactly
  // get_cipher_length_in_bytes() bytes long. Returns the recovered plaintext.
  //
  // Not a const method: OpenSSL's EVP_PKEY_CTX_new() takes a non-const
  // EVP_PKEY *, mirroring how cipher_context::update() / finalize() are
  // non-const for the same reason.
  [[nodiscard]] std::string decrypt_oaep(util::const_byte_span ciphertext);

private:
  struct native_helper;
  struct impl_deleter {
    void operator()(void *pkey) const noexcept;
  };

  using impl_ptr = std::unique_ptr<void, impl_deleter>;
  impl_ptr impl_;
};

} // namespace opensslpp

#endif // OPENSSLPP_RSA_PRIVATE_KEY_HPP
