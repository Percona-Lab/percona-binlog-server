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

#include "opensslpp/rsa_private_key.hpp"

#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/types.h>

#include "opensslpp/core_error.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace opensslpp {

namespace {

struct bio_deleter {
  void operator()(BIO *bio) const noexcept { BIO_free(bio); }
};

using bio_ptr = std::unique_ptr<BIO, bio_deleter>;

struct pkey_ctx_deleter {
  void operator()(EVP_PKEY_CTX *ctx) const noexcept { EVP_PKEY_CTX_free(ctx); }
};

using pkey_ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, pkey_ctx_deleter>;

} // anonymous namespace

struct rsa_private_key::native_helper {
  [[nodiscard]] static auto deimpl(auto &impl) noexcept {
    using cast_type = std::conditional_t<
        std::is_const_v<std::remove_reference_t<decltype(impl)>>,
        const EVP_PKEY, EVP_PKEY>;
    return static_cast<cast_type *>(impl.get());
  }
};

void rsa_private_key::impl_deleter::operator()(void *pkey) const noexcept {
  if (pkey != nullptr) {
    EVP_PKEY_free(static_cast<EVP_PKEY *>(pkey));
  }
}

rsa_private_key::rsa_private_key(std::string_view pem) {
  if (std::empty(pem)) {
    util::exception_location().raise<core_error>(
        "empty PEM buffer for RSA private key");
  }
  if (!std::in_range<int>(std::size(pem))) {
    util::exception_location().raise<core_error>(
        "PEM buffer size is out of range");
  }

  const bio_ptr bio{
      BIO_new_mem_buf(std::data(pem), static_cast<int>(std::size(pem))),
      bio_deleter{}};
  if (!bio) {
    util::exception_location().raise<core_error>(
        "cannot allocate PEM memory BIO for RSA private key");
  }

  impl_ptr new_impl{
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr),
      impl_deleter{}};
  if (!new_impl) {
    util::exception_location().raise<core_error>(
        "cannot parse RSA private key from PEM");
  }

  impl_ = std::move(new_impl);
}

std::size_t rsa_private_key::get_cipher_length_in_bytes() const noexcept {
  assert(impl_);
  const auto native_size{EVP_PKEY_get_size(native_helper::deimpl(impl_))};
  assert(native_size > 0);
  return static_cast<std::size_t>(native_size);
}

std::string rsa_private_key::decrypt_oaep(util::const_byte_span ciphertext) {
  assert(impl_);

  const auto cipher_length{get_cipher_length_in_bytes()};
  if (std::size(ciphertext) != cipher_length) {
    util::exception_location().raise<core_error>(
        "RSA ciphertext has unexpected length");
  }

  // decrypt_oaep is intentionally non-const: OpenSSL's EVP_PKEY_CTX_new()
  // takes a non-const EVP_PKEY *, so keeping the underlying handle mutable
  // through the wrapper matches cipher_context::update() / finalize(), which
  // are also non-const because their EVP_CIPHER_CTX_* C calls require it.
  const pkey_ctx_ptr key_ctx{
      EVP_PKEY_CTX_new(native_helper::deimpl(impl_), nullptr),
      pkey_ctx_deleter{}};
  if (!key_ctx) {
    util::exception_location().raise<core_error>(
        "cannot allocate RSA decrypt context");
  }

  if (EVP_PKEY_decrypt_init(key_ctx.get()) <= 0) {
    util::exception_location().raise<core_error>(
        "cannot initialize RSA decrypt context");
  }
  if (EVP_PKEY_CTX_set_rsa_padding(key_ctx.get(), RSA_PKCS1_OAEP_PADDING) <=
      0) {
    util::exception_location().raise<core_error>(
        "cannot select OAEP padding on RSA decrypt context");
  }

  // The plaintext is at most cipher_length bytes long; size the buffer to
  // that upper bound and shrink to the exact length reported by
  // EVP_PKEY_decrypt().
  std::string plain_text(cipher_length, '\0');
  std::size_t plain_text_length{cipher_length};

  if (EVP_PKEY_decrypt(
          key_ctx.get(),
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<unsigned char *>(std::data(plain_text)),
          &plain_text_length,
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<const unsigned char *>(std::data(ciphertext)),
          std::size(ciphertext)) <= 0) {
    util::exception_location().raise<core_error>("cannot RSA-OAEP decrypt");
  }

  plain_text.resize(plain_text_length);
  return plain_text;
}

} // namespace opensslpp
