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

#include <cassert>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/types.h>

#include "opensslpp/core_error.hpp"
#include "opensslpp/encryption_padding.hpp"
#include "opensslpp/evp_pkey.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace opensslpp {

struct evp_pkey::native_helper {
  [[nodiscard]] static auto deimpl(auto &impl) noexcept {
    using cast_type = std::conditional_t<
        std::is_const_v<std::remove_reference_t<decltype(impl)>>,
        const EVP_PKEY, EVP_PKEY>;
    return static_cast<cast_type *>(impl.get());
  }
  [[nodiscard]] static auto deimpl_const_casted(auto &impl) noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<EVP_PKEY *>(static_cast<const EVP_PKEY *>(impl.get()));
  }

  static void validate_algorithm_name(std::string_view algorithm_name) {
    if (algorithm_name != evp_pkey::get_rsa_algorithm_name()) {
      util::exception_location().raise<std::logic_error>(
          "current implementation of EVP_PKEY wrapper does not support the "
          "specified algorithm");
    }
  }

  [[nodiscard]] static int encryption_padding_to_native_rsa_padding(
      encryption_padding padding) noexcept {
    int res{RSA_NO_PADDING};
    switch (padding) {
    case encryption_padding::no:
      res = RSA_NO_PADDING;
      break;
    case encryption_padding::pkcs1:
      res = RSA_PKCS1_PADDING;
      break;
    case encryption_padding::pkcs1_oaep:
      res = RSA_PKCS1_OAEP_PADDING;
      break;
    default:
      assert(false);
    }
    return res;
  }

  struct bio_deleter {
    void operator()(BIO *bio) const noexcept {
      if (bio != nullptr) {
        BIO_free(bio);
      }
    }
  };
  using bio_ptr = std::unique_ptr<BIO, bio_deleter>;
  [[nodiscard]] static bio_ptr create_mem_sink_bio() {
    bio_ptr res{BIO_new(BIO_s_mem())};
    if (!res) {
      util::exception_location().raise<core_error>(
          "cannot allocate memory sink BIO");
    }
    return res;
  }
  [[nodiscard]] static bio_ptr create_mem_source_bio(std::string_view data) {
    // BIO_new_mem_buf() treats negative length (-1) as a request to calculate
    // the length via strlen(), so sizes not representable as int must be
    // rejected explicitly
    if (!std::in_range<int>(std::size(data))) {
      util::exception_location().raise<core_error>(
          "data is too large for memory source BIO");
    }
    bio_ptr res{
        BIO_new_mem_buf(std::data(data), static_cast<int>(std::size(data)))};
    if (!res) {
      util::exception_location().raise<core_error>(
          "cannot allocate memory source BIO");
    }
    return res;
  }

  // passing nullptr as a password callback to PEM_read_bio_*() functions
  // makes OpenSSL fall back to PEM_def_callback(), which interactively
  // prompts for a passphrase on the terminal when the PEM data is encrypted;
  // this callback makes such imports fail immediately instead
  [[nodiscard]] static int
  no_passphrase_callback(char * /*buf*/, int /*size*/, int /*rwflag*/,
                         void * /*user_data*/) noexcept {
    return 0;
  }

  [[nodiscard]] static std::string bio_to_string(const bio_ptr &bio) {
    char *bio_mem_ptr{nullptr};
    const long bio_mem_len{BIO_get_mem_data(bio.get(), &bio_mem_ptr)};
    if (bio_mem_ptr == nullptr || bio_mem_len <= 0) {
      util::exception_location().raise<core_error>(
          "cannot extract string from BIO");
    }
    return std::string{bio_mem_ptr, static_cast<std::size_t>(bio_mem_len)};
  }

  struct evp_pkey_ctx_deleter {
    void operator()(EVP_PKEY_CTX *ctx) const noexcept {
      if (ctx != nullptr) {
        EVP_PKEY_CTX_free(ctx);
      }
    }
  };
  using evp_pkey_ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, evp_pkey_ctx_deleter>;

  struct ossl_param_deleter {
    void operator()(OSSL_PARAM *params) const noexcept {
      if (params != nullptr) {
        OSSL_PARAM_free(params);
      }
    }
  };
  using ossl_param_ptr = std::unique_ptr<OSSL_PARAM, ossl_param_deleter>;
};

void evp_pkey::evp_pkey_deleter::operator()(void *evp_pkey) const noexcept {
  if (evp_pkey != nullptr) {
    EVP_PKEY_free(static_cast<EVP_PKEY *>(evp_pkey));
  }
}

std::size_t evp_pkey::get_size_in_bits() const noexcept {
  assert(!is_empty());
  return static_cast<std::size_t>(
      EVP_PKEY_get_bits(native_helper::deimpl(impl_)));
}

std::size_t evp_pkey::get_size_in_bytes() const noexcept {
  assert(!is_empty());
  return static_cast<std::size_t>(
      EVP_PKEY_get_size(native_helper::deimpl(impl_)));
}

[[nodiscard]] std::size_t evp_pkey::get_max_encryption_message_size(
    encryption_padding padding) const noexcept {
  assert(!is_empty());
  static constexpr auto pkcs1_oaep_padding_size{42UZ};
  std::size_t padding_bytes{0UZ};
  switch (padding) {
  case encryption_padding::no:
    padding_bytes = 0;
    break;
  case encryption_padding::pkcs1:
    padding_bytes = RSA_PKCS1_PADDING_SIZE;
    break;
  case encryption_padding::pkcs1_oaep:
    // for some reason, <rsa.h> does not have a constant for max padding
    // size for RSA_PKCS1_OAEP_PADDING
    // the number is taken from here
    // https://www.openssl.org/docs/man1.1.1/man3/RSA_public_encrypt.html
    padding_bytes = pkcs1_oaep_padding_size;
    break;
  }
  const std::size_t block_size{get_size_in_bytes()};
  return block_size > padding_bytes ? block_size - padding_bytes : 0;
}

evp_pkey evp_pkey::derive_public_key() const {
  assert(!is_empty());

  OSSL_PARAM *native_params{nullptr};

  if (EVP_PKEY_todata(native_helper::deimpl(impl_), EVP_PKEY_PUBLIC_KEY,
                      &native_params) != 1) {
    util::exception_location().raise<core_error>(
        "cannot extract public key data from EVP_PKEY");
  }
  const native_helper::ossl_param_ptr params{native_params};

  const auto *type_name{EVP_PKEY_get0_type_name(native_helper::deimpl(impl_))};
  const native_helper::evp_pkey_ctx_ptr ctx{
      EVP_PKEY_CTX_new_from_name(nullptr, type_name, nullptr)};
  if (!ctx) {
    util::exception_location().raise<core_error>(
        "cannot create EVP_PKEY_CTX for public key derivation");
  }
  if (EVP_PKEY_fromdata_init(ctx.get()) != 1) {
    util::exception_location().raise<core_error>(
        "cannot initialize EVP_PKEY_CTX for public key derivation");
  }
  EVP_PKEY *pub_native{nullptr};
  if (EVP_PKEY_fromdata(ctx.get(), &pub_native, EVP_PKEY_PUBLIC_KEY,
                        params.get()) != 1) {
    util::exception_location().raise<core_error>(
        "cannot import public key data to EVP_PKEY");
  }

  return evp_pkey{pub_native, false};
}

[[nodiscard]] evp_pkey evp_pkey::generate(const std::string &algorithm_name,
                                          std::size_t bits) {
  native_helper::validate_algorithm_name(algorithm_name);

  const native_helper::evp_pkey_ctx_ptr ctx{
      EVP_PKEY_CTX_new_from_name(nullptr, algorithm_name.c_str(), nullptr)};
  if (!ctx) {
    util::exception_location().raise<core_error>(
        "cannot create EVP_PKEY_CTX for key generation");
  }

  if (EVP_PKEY_keygen_init(ctx.get()) <= 0) {
    util::exception_location().raise<core_error>(
        "cannot initialize EVP_PKEY_CTX for key generation");
  }

  if (algorithm_name == evp_pkey::get_rsa_algorithm_name()) {
    if (!std::in_range<int>(bits)) {
      util::exception_location().raise<core_error>(
          "RSA keygen bits out of range");
    }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), static_cast<int>(bits)) <=
        0) {
      util::exception_location().raise<core_error>(
          "cannot set RSA keygen bits");
    }
  } else {
    // TODO: implement setting bit parameters for other algorithms here
    assert(false);
  }

  EVP_PKEY *pkey_native{nullptr};
  if (EVP_PKEY_generate(ctx.get(), &pkey_native) <= 0) {
    util::exception_location().raise<core_error>("cannot generate EVP_PKEY");
  }

  return evp_pkey{pkey_native, true};
}

/*static*/
std::string evp_pkey::export_private_pem(const evp_pkey &key) {
  assert(!key.is_empty());

  if (!key.has_private_component()) {
    util::exception_location().raise<core_error>(
        "EVP_PKEY does not have private components");
  }

  const native_helper::bio_ptr sink{native_helper::create_mem_sink_bio()};
  const auto native_result{
      PEM_write_bio_PrivateKey(sink.get(), native_helper::deimpl(key.impl_),
                               nullptr, nullptr, 0, nullptr, nullptr)};

  if (native_result <= 0) {
    util::exception_location().raise<core_error>(
        "cannot export EVP_PKEY key to PEM PRIVATE KEY");
  }

  return native_helper::bio_to_string(sink);
}

/*static*/
std::string evp_pkey::export_public_pem(const evp_pkey &key) {
  assert(!key.is_empty());

  const native_helper::bio_ptr sink{native_helper::create_mem_sink_bio()};
  const auto native_result{
      PEM_write_bio_PUBKEY(sink.get(), native_helper::deimpl(key.impl_))};
  if (native_result == 0) {
    util::exception_location().raise<core_error>(
        "cannot export EVP_PKEY key to PEM PUBLIC KEY");
  }

  return native_helper::bio_to_string(sink);
}

/*static*/
evp_pkey evp_pkey::import_private_pem(std::string_view pem) {
  const native_helper::bio_ptr source{
      native_helper::create_mem_source_bio(pem)};
  evp_pkey res{PEM_read_bio_PrivateKey(source.get(), nullptr,
                                       &native_helper::no_passphrase_callback,
                                       nullptr),
               true};
  if (res.is_empty()) {
    util::exception_location().raise<core_error>(
        "cannot import EVP_PKEY from PEM PRIVATE KEY");
  }

  res.validate_algorithm_internal();

  return res;
}

/*static*/
evp_pkey evp_pkey::import_public_pem(std::string_view pem) {
  const native_helper::bio_ptr source{
      native_helper::create_mem_source_bio(pem)};
  evp_pkey res{PEM_read_bio_PUBKEY(source.get(), nullptr,
                                   &native_helper::no_passphrase_callback,
                                   nullptr),
               false};
  if (res.is_empty()) {
    util::exception_location().raise<core_error>(
        "cannot import EVP_PKEY from PEM PUBLIC KEY");
  }

  res.validate_algorithm_internal();

  return res;
}

std::string_view evp_pkey::get_type_name_internal() const noexcept {
  assert(!is_empty());
  // EVP_PKEY_get0_type_name() may return nullptr, which cannot be safely
  // compared with std::string, so it is converted to an empty string view
  const char *const type_name{
      EVP_PKEY_get0_type_name(native_helper::deimpl(impl_))};
  return type_name != nullptr ? std::string_view{type_name}
                              : std::string_view{};
}

void evp_pkey::validate_algorithm_internal() const {
  native_helper::validate_algorithm_name(get_type_name_internal());
}

[[nodiscard]] util::dynamic_byte_buffer
evp_key_encrypt(util::const_byte_span data, const evp_pkey &key,
                encryption_padding padding) {
  assert(!key.is_empty());

  // TODO: implement for other algorithms besides RSA
  assert(key.get_type_name_internal() == evp_pkey::get_rsa_algorithm_name());

  const auto max_message_size{key.get_max_encryption_message_size(padding)};
  if (padding == encryption_padding::no &&
      std::size(data) != max_message_size) {
    util::exception_location().raise<core_error>(
        "for EVP_PKEY encryption with no padding the message size must match "
        "the key size");
  } else {
    if (std::size(data) > max_message_size) {
      util::exception_location().raise<core_error>(
          "for EVP_PKEY encryption the message size must not exceed the "
          "maximum allowed by the key size and padding");
    }
  }

  const auto native_padding{
      evp_pkey::native_helper::encryption_padding_to_native_rsa_padding(
          padding)};

  const evp_pkey::native_helper::evp_pkey_ctx_ptr ctx{EVP_PKEY_CTX_new(
      evp_pkey::native_helper::deimpl_const_casted(key.impl_), nullptr)};
  if (!ctx) {
    util::exception_location().raise<core_error>(
        "cannot allocate EVP_PKEY encryption context");
  }

  if (EVP_PKEY_encrypt_init(ctx.get()) <= 0) {
    util::exception_location().raise<core_error>(
        "cannot initialize EVP_PKEY encryption context");
  }
  if (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), native_padding) <= 0) {
    util::exception_location().raise<core_error>(
        "cannot set the specified padding for EVP_PKEY encryption context");
  }

  // the size of the encryption result is always equal to the key size in
  // bytes regardless of the padding
  util::dynamic_byte_buffer res(key.get_size_in_bytes());
  std::size_t res_size{std::size(res)};

  if (EVP_PKEY_encrypt(
          ctx.get(),
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<unsigned char *>(std::data(res)), &res_size,
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<const unsigned char *>(std::data(data)),
          std::size(data)) <= 0) {
    util::exception_location().raise<core_error>(
        "cannot encrypt data with EVP_PKEY");
  }
  if (res_size != key.get_size_in_bytes()) {
    util::exception_location().raise<core_error>(
        "unexpected EVP_PKEY encryption result size");
  }

  return res;
}

[[nodiscard]] util::dynamic_byte_buffer
evp_key_decrypt(util::const_byte_span data, const evp_pkey &key,
                encryption_padding padding) {
  assert(!key.is_empty());

  // TODO: implement for other algorithms besides RSA
  assert(key.get_type_name_internal() == evp_pkey::get_rsa_algorithm_name());

  if (std::size(data) != key.get_size_in_bytes()) {
    util::exception_location().raise<core_error>(
        "for EVP_PKEY decryption the message size must match the key size");
  }

  const auto native_padding{
      evp_pkey::native_helper::encryption_padding_to_native_rsa_padding(
          padding)};

  const evp_pkey::native_helper::evp_pkey_ctx_ptr ctx{EVP_PKEY_CTX_new(
      evp_pkey::native_helper::deimpl_const_casted(key.impl_), nullptr)};
  if (!ctx) {
    util::exception_location().raise<core_error>(
        "cannot allocate EVP_PKEY decryption context");
  }

  if (EVP_PKEY_decrypt_init(ctx.get()) <= 0) {
    util::exception_location().raise<core_error>(
        "cannot initialize EVP_PKEY decryption context");
  }
  if (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), native_padding) <= 0) {
    util::exception_location().raise<core_error>(
        "cannot set the specified padding for EVP_PKEY decryption context");
  }

  // the real size of the decryption result does not exceed the length of
  // the encrypted message
  util::dynamic_byte_buffer res(key.get_size_in_bytes());
  std::size_t res_size{res.size()};

  if (EVP_PKEY_decrypt(
          ctx.get(),
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<unsigned char *>(std::data(res)), &res_size,
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<const unsigned char *>(std::data(data)),
          std::size(data)) <= 0) {
    util::exception_location().raise<core_error>(
        "cannot decrypt data with EVP_PKEY");
  }
  if (res_size > key.get_max_encryption_message_size(padding)) {
    util::exception_location().raise<core_error>(
        "unexpected EVP_PKEY decryption result size");
  }
  res.resize(res_size);
  return res;
}

} // namespace opensslpp
