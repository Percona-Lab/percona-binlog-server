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
#include <cstdint>
#include <exception>
#include <fstream>
#include <functional>
#include <ios>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/types.h>

#include "minimysql/network_io_operations_fwd.hpp"

// clang-format off
// caching_sha2_password authentication flow (minimysql mock):
//
//   Client                              Server (this authenticator)
//     |                                         |
//     |--- Handshake (plugin, scramble) ------->|
//     |                                         |
//     |<-- AuthSwitch (plugin mismatch only) ---|  needs_auth_method_switch()
//     |--- Auth response (scramble) ----------->|  begin_authentication()
//     |                                         |
//     |   [fast path — always tried first; no SHA2 digest cache; behaves as
//     |    permanent cache hit via verify_greeting_scramble(), even first conn]
//     |                                         |
//     |<-- AuthMoreData 0x01|0x03 --------------|  scramble matches password
//     |<-- OK ----------------------------------|
//     |                                         |
//     |   [full auth — when scramble does not match; real server: cache miss]
//     |                                         |
//     |<-- AuthMoreData 0x01|0x04 --------------|  perform full authentication
//     |                                         |
//     |   Client chooses password encoding (server accepts per transport):
//     |                                         |
//     |   (A) secure transport [TLS stub; connection_is_secure() false today]
//     |--- cleartext password (0-terminated) ->|  verify_cleartext_password()
//     |<-- OK / Access denied ------------------|
//     |                                         |
//     |   (B) plain TCP — client opts in to RSA (server expects ciphertext)
//     |--- 0x02 Request public key (optional) ->|  --get-server-public-key
//     |<-- AuthMoreData 0x01|PEM ---------------|  enqueue_public_key()
//     |                                         |  (skip 0x02 via
//     |                                         |   --server-public-key-path)
//     |--- RSA-OAEP encrypted password -------->|  verify_encrypted_password()
//     |<-- OK / Access denied ------------------|
//     |                                         |
//     |   (C) plain TCP, no RSA flags — client fails locally before sending
//     |       ("Authentication requires secure connection.")
// clang-format on
namespace {

enum class digest_code_type : std::uint8_t {
  sha256,
};

class digest_context {
public:
  explicit digest_context(digest_code_type digest_code)
      : impl_{EVP_MD_CTX_new(), digest_context_deleter{}} {
    if (!impl_) {
      throw std::runtime_error{"failed to create digest context"};
    }
    if (EVP_DigestInit_ex(impl_.get(), get_md_by_digest_code(digest_code),
                          nullptr) == 0) {
      throw std::runtime_error{"failed to initialize digest context"};
    }
  }

  ~digest_context() noexcept = default;

  digest_context(const digest_context &obj) = delete;
  digest_context(digest_context &&obj) noexcept = delete;

  digest_context &operator=(const digest_context &obj) = delete;
  digest_context &operator=(digest_context &&obj) noexcept = delete;

  [[nodiscard]] std::size_t get_size_in_bytes() const noexcept {
    assert(impl_);
    auto native_result{EVP_MD_CTX_size(impl_.get())};
    assert(native_result != -1);
    return static_cast<std::size_t>(native_result);
  }

  void update(std::string_view data) {
    assert(impl_);
    if (EVP_DigestUpdate(impl_.get(), std::data(data), std::size(data)) == 0) {
      throw std::runtime_error{"failed to update digest context"};
    }
  }
  std::string finalize() {
    assert(impl_);
    std::string result(get_size_in_bytes(), '\0');

    unsigned int result_size = 0;
    if (EVP_DigestFinal_ex(
            impl_.get(),
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<unsigned char *>(std::data(result)),
            &result_size) == 0) {
      throw std::runtime_error{"cannot finalize digest context"};
    }
    assert(result_size == std::size(result));

    impl_.reset();
    return result;
  }

private:
  struct digest_context_deleter {
    void operator()(EVP_MD_CTX *digest_context) const noexcept {
      // null-ness is handled by EVP_MD_CTX_free
      EVP_MD_CTX_free(digest_context);
    }
  };

  using impl_ptr = std::unique_ptr<EVP_MD_CTX, digest_context_deleter>;
  impl_ptr impl_;

  [[nodiscard]] static const EVP_MD *
  get_md_by_digest_code(digest_code_type digest_code) noexcept {
    switch (digest_code) {
    case digest_code_type::sha256:
      return EVP_sha256();
    default:
      // should never happen as we only construct digest_context with supported
      // digest_code_type
      return nullptr;
    }
  }
};

std::string calculate_digest(digest_code_type digest_code,
                             std::string_view data) {
  digest_context ctx(digest_code);
  ctx.update(data);
  return ctx.finalize();
}

void xor_with_pattern(std::span<char> data, std::string_view pattern) {
  if (std::empty(pattern)) {
    return;
  }

  for (std::size_t index{0U}; index < std::size(data); ++index) {
    data[index] = static_cast<char>(
        static_cast<unsigned char>(data[index]) ^
        static_cast<unsigned char>(pattern[index % std::size(pattern)]));
  }
}

constexpr std::uint8_t request_public_key{0x02U};
constexpr std::uint8_t perform_full_authentication{0x04U};

// Embedded server RSA keys used when no server_rsa_*_key_path is supplied.
// Temporary for standalone minimysql; binlog server integration should pass
// paths from config via network_service.
constexpr std::string_view default_rsa_public_key_pem{
    "-----BEGIN PUBLIC KEY-----\n"
    "MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAvV2VNbsQPG0Bh0KC8F4z\n"
    "CGXvMNcSicCiLXxeLWrJsmKZl0ggf2ydymYUUewq+dVxDdh85sdSvxEmtIWvKSRK\n"
    "+RRCAURztq2Succd+24SF5IZYjlIJE/U0AYUxHzUcOsannfzui60IaTHpcBFHTJK\n"
    "6myxGx9MORZmhfv580mfvz4yvgLjS5yGOIS6rlxD9YV1Y04Rx3SXQQBnC7rDBL91\n"
    "ktNWvbclsonfytY19N9p+Gprms30yRT+BmPFB7TqpReeZa3ivg15g/z3BLNyvj3Y\n"
    "KiQM3cd7ENJC2x2LRxL5pG684cFNStSjT4FvA+oh45UnU45aOSEjrxNkBG8ci0e+\n"
    "VKX539rK+nDzTE/MHpnvfHp4DB+kSYBPuKHY2Eaw31NwPpfLWwEJPiDrktJJmRZq\n"
    "ENMHLXksdiqGhvYmI33wZaZAfjbDZFMfPF5yBMBGDZ3aeNz5Le7uqS6g6XMOoiz/\n"
    "d2S5RzRrCol1yqCBPtODjfFPC4K8GGYVkWZgSCf/PRt/DgDnZOfZSSYIQNeyr21e\n"
    "mqgqQ+yhXEGKVjcDTKcbSLiWAdA+GkAzLAXXhafM8mrhpnGKdO4Or6ySz7G1vk2J\n"
    "t2ZSdP740oVSJi59P9NEgXcbd3c4FzjXSOOsxfhPQfobUk3ikt55lN3fBX3mBvUd\n"
    "uxNhAcQ02ZD5zXrX6+loiV8CAwEAAQ==\n"
    "-----END PUBLIC KEY-----\n"};

constexpr std::string_view default_rsa_private_key_pem{
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "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\n"
    "-----END RSA PRIVATE KEY-----\n"};

struct evp_pkey_deleter {
  void operator()(EVP_PKEY *key) const noexcept { EVP_PKEY_free(key); }
};

using evp_pkey_ptr = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>;

[[nodiscard]] evp_pkey_ptr load_private_key(std::string_view pem) {
  BIO *bio{BIO_new_mem_buf(std::data(pem), static_cast<int>(std::size(pem)))};
  if (bio == nullptr) {
    throw std::runtime_error{"failed to allocate OpenSSL BIO for private key"};
  }

  evp_pkey_ptr key{PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr),
                   evp_pkey_deleter{}};
  BIO_free(bio);

  if (!key) {
    throw std::runtime_error{"failed to parse RSA private key"};
  }

  return key;
}

[[nodiscard]] std::string read_pem_file(std::string_view path) {
  // Avoid istreambuf_iterator: GCC 14 -Wnull-dereference false positive under
  // -O2.
  std::ifstream file{std::string{path}, std::ios::binary};
  if (!file) {
    throw std::runtime_error{"failed to open PEM file: " + std::string{path}};
  }
  if (!file.seekg(0, std::ios_base::end)) {
    throw std::runtime_error{"failed to seek PEM file: " + std::string{path}};
  }
  const auto end_offset{static_cast<std::streamoff>(file.tellg())};
  if (end_offset < 0) {
    throw std::runtime_error{"failed to size PEM file: " + std::string{path}};
  }
  if (!file.seekg(0, std::ios_base::beg)) {
    throw std::runtime_error{"failed to rewind PEM file: " + std::string{path}};
  }

  std::string contents(static_cast<std::size_t>(end_offset), '\0');
  if (end_offset != 0 && !file.read(std::data(contents), end_offset)) {
    throw std::runtime_error{"failed to read PEM file: " + std::string{path}};
  }
  return contents;
}

} // anonymous namespace

namespace minimysql {

struct caching_sha2_password_authenticator::rsa_key_pair {
  rsa_key_pair(std::string public_key_pem, std::string_view private_key_pem)
      : public_key_pem_{std::move(public_key_pem)},
        private_key_{load_private_key(private_key_pem)},
        cipher_length_{
            static_cast<std::size_t>(EVP_PKEY_get_size(private_key_.get()))} {}

  static std::unique_ptr<rsa_key_pair>
  from_paths(std::string_view server_rsa_public_key_path,
             std::string_view server_rsa_private_key_path) {
    return std::make_unique<rsa_key_pair>(
        read_pem_file(server_rsa_public_key_path),
        read_pem_file(server_rsa_private_key_path));
  }

  static std::unique_ptr<rsa_key_pair> embedded_default() {
    return std::make_unique<rsa_key_pair>(
        std::string{default_rsa_public_key_pem},
        std::string{default_rsa_private_key_pem});
  }

  [[nodiscard]] std::string_view public_key_pem() const noexcept {
    return public_key_pem_;
  }

  [[nodiscard]] std::size_t cipher_length() const noexcept {
    return cipher_length_;
  }

  [[nodiscard]] std::string
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  decrypt_password(std::string_view encrypted_password,
                   std::string_view salt) const {
    if (std::size(encrypted_password) != cipher_length_) {
      throw std::runtime_error{"encrypted password has unexpected length"};
    }

    std::string plain_text(cipher_length_ + 1U, '\0');
    std::size_t plain_text_length{cipher_length_};

    EVP_PKEY_CTX *key_ctx{EVP_PKEY_CTX_new(private_key_.get(), nullptr)};
    if (key_ctx == nullptr) {
      throw std::runtime_error{"failed to create RSA decrypt context"};
    }

    if (EVP_PKEY_decrypt_init(key_ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(key_ctx, RSA_PKCS1_OAEP_PADDING) <= 0 ||
        EVP_PKEY_decrypt(
            key_ctx,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<unsigned char *>(std::data(plain_text)),
            &plain_text_length,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<const unsigned char *>(
                std::data(encrypted_password)),
            std::size(encrypted_password)) <= 0) {
      EVP_PKEY_CTX_free(key_ctx);
      throw std::runtime_error{"failed to decrypt RSA password"};
    }
    EVP_PKEY_CTX_free(key_ctx);

    xor_with_pattern(std::span{std::data(plain_text), cipher_length_ + 1U},
                     salt);

    const auto password_end{plain_text.find('\0')};
    if (password_end == std::string::npos) {
      throw std::runtime_error{"decrypted password is missing a terminator"};
    }

    return plain_text.substr(0, password_end);
  }

private:
  std::string public_key_pem_;
  evp_pkey_ptr private_key_;
  std::size_t cipher_length_;
};

caching_sha2_password_authenticator::caching_sha2_password_authenticator(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view password, std::string_view server_rsa_public_key_path,
    std::string_view server_rsa_private_key_path)
    : password_{password} {
  const bool has_public{!std::empty(server_rsa_public_key_path)};
  const bool has_private{!std::empty(server_rsa_private_key_path)};
  if (has_public != has_private) {
    throw std::runtime_error{
        "server_rsa_public_key_path and server_rsa_private_key_path must both "
        "be set or both be empty"};
  }
  if (has_public) {
    rsa_keys_ = rsa_key_pair::from_paths(server_rsa_public_key_path,
                                         server_rsa_private_key_path);
  } else {
    rsa_keys_ = rsa_key_pair::embedded_default();
  }
}

caching_sha2_password_authenticator::~caching_sha2_password_authenticator() =
    default;

// AuthSwitch only when the client plugin does not match. A cache miss on an
// already-matching plugin is handled by sending 0x04, not by restarting auth.
bool caching_sha2_password_authenticator::needs_auth_method_switch(
    std::string_view client_plugin) noexcept {
  return client_plugin != plugin_name;
}

std::string
caching_sha2_password_authenticator::generate_auth_switch_plugin_data(
    std::string_view salt) {
  return std::string{salt} + '\0';
}

void caching_sha2_password_authenticator::begin_authentication(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view expected_username, std::string_view client_username,
    std::string_view client_auth_data, std::string_view salt,
    bool secure_transport, auth_packet_encoder &encoder) {
  expected_username_ = expected_username;
  client_username_ = client_username;
  salt_ = salt;
  secure_transport_ = secure_transport;
  outbound_frames_.clear();
  phase_ = phase::idle;
  // Minimysql has no SHA2 digest cache. We always behave as if fast auth had a
  // cache hit: verify the handshake scramble against the configured password
  // directly, even on the first connection. A real server would send 0x04 here
  // on cache miss and require RSA (plain TCP) or cleartext over SSL for full
  // auth (see verify_cleartext_password(); not reached until TLS exists).
  // Alternative: skip verify_greeting_scramble() and always enqueue full
  // authentication (0x04) to mirror first-login / cache-miss behavior.

  // NOTE: Disable the below greeting scramble verification to enable full
  // authentication with public key exchange.
  if (/* false && */ verify_greeting_scramble(
      expected_username_, client_username_, client_auth_data, salt_)) {
    enqueue_fast_auth_success(encoder);
    phase_ = phase::succeeded;
    return;
  }

  enqueue_perform_full_authentication(encoder);
  phase_ = phase::awaiting_full_auth_response;
}

authentication_state
caching_sha2_password_authenticator::state() const noexcept {
  switch (phase_) {
  case phase::succeeded:
    return authentication_state::succeeded;
  case phase::failed:
    return authentication_state::failed;
  default:
    return authentication_state::in_progress;
  }
}

// True while the server must read another client AuthMoreData frame:
// - awaiting_full_auth_response: client replies to 0x04 with either 0x02
//   (request PEM) or RSA ciphertext when it already has the public key, or with
//   a cleartext password when secure_transport_ is true (SSL/TLS stub).
// - awaiting_encrypted_password: client sends ciphertext after receiving PEM.
bool caching_sha2_password_authenticator::expects_client_input()
    const noexcept {
  return phase_ == phase::awaiting_full_auth_response ||
         phase_ == phase::awaiting_encrypted_password;
}

std::vector<network_buffer_type>
caching_sha2_password_authenticator::take_outbound_frames() {
  return std::exchange(outbound_frames_, {});
}

authentication_state caching_sha2_password_authenticator::submit_client_frame(
    const network_buffer_type &frame, auth_packet_encoder &encoder) {
  encoder.validate_incoming_sequence(frame);
  const std::string_view payload{encoder.frame_payload(frame)};

  if (phase_ == phase::awaiting_full_auth_response) {
    if (secure_transport_) {
      return verify_cleartext_password(payload);
    }

    // After 0x04 the client may send 0x02 to fetch PEM
    // (--get-server-public-key) or send RSA ciphertext immediately when it
    // already loaded the key from disk
    // (--server-public-key-path).
    if (check_public_key_request(payload)) {
      enqueue_public_key(encoder);
      phase_ = phase::awaiting_encrypted_password;
      return authentication_state::in_progress;
    }

    return verify_encrypted_password(payload);
  }

  if (phase_ == phase::awaiting_encrypted_password) {
    return verify_encrypted_password(payload);
  }

  phase_ = phase::failed;
  return authentication_state::failed;
}

std::string caching_sha2_password_authenticator::scramble(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view password, std::string_view salt) {
  const auto digest_code{digest_code_type::sha256};

  auto result{calculate_digest(digest_code, password)};
  const auto double_hashed_password{calculate_digest(digest_code, result)};

  digest_context ctx(digest_code);
  ctx.update(double_hashed_password);
  ctx.update(salt);
  const auto salted_triple_hashed_password{ctx.finalize()};

  assert(std::size(result) == std::size(salted_triple_hashed_password));
  std::ranges::transform(result, salted_triple_hashed_password,
                         std::begin(result), std::bit_xor<std::uint8_t>{});
  return result;
}

std::string_view
caching_sha2_password_authenticator::get_rsa_public_key_pem() const noexcept {
  return rsa_keys_->public_key_pem();
}

std::size_t
caching_sha2_password_authenticator::get_rsa_cipher_length() const noexcept {
  return rsa_keys_->cipher_length();
}

bool caching_sha2_password_authenticator::check_public_key_request(
    std::string_view payload) noexcept {
  return std::size(payload) == 1U &&
         static_cast<std::uint8_t>(payload.front()) == request_public_key;
}

std::string caching_sha2_password_authenticator::decrypt_rsa_password(
    std::string_view encrypted_password, std::string_view salt) const {
  return rsa_keys_->decrypt_password(encrypted_password, salt);
}

// Outbound caching_sha2_password plugin packets after the handshake must be
// framed as AuthMoreData on the wire:
//
//   MySQL frame payload = 0x01 || <plugin data>
//
// where <plugin data> is one of:
//   - 0x03  fast auth success
//   - 0x04  perform full authentication
//   - PEM   server RSA public key after a client 0x02 request
//
// On a real MySQL / Percona Server, the auth plugin does NOT prepend 0x01
// itself. It calls MYSQL_PLUGIN_VIO::write_packet() with the raw plugin bytes
// (e.g. a single 0x04, or the PEM string). The server mpvio layer then wraps
// that payload via wrap_plguin_data_into_proper_command() /
// net_write_command(..., command=1, ...), which is AuthMoreData. See
// sql/auth/sql_authentication.cc (server_mpvio_write_packet) and
// sql/auth/sha2_password.cc (write_packet of perform_full_authentication /
// public key PEM).
//
// Minimysql has no mpvio / plugin VIO. We write classic-protocol frames
// directly, so encode_auth_method_data() must supply the AuthMoreData 0x01
// status byte that the real server would have added for us.
//
// On the client side, client_mpvio_read_packet() (sql-common/client.cc) strips
// a leading 0x01 when present before handing data to the auth plugin. That is
// why the plugin logic checks for a 1-byte 0x03 / 0x04, and why PEM_read sees
// a clean "-----BEGIN PUBLIC KEY-----" buffer rather than a 0x01-prefixed PEM.
//
// Sending raw 0x04 or raw PEM without the 0x01 prefix can still interoperate
// with some clients (the strip is conditional), but it diverges from the
// server protocol and from the fast-auth path, which already used AuthMoreData.
// Always use encode_auth_method_data() for these continuations.
void caching_sha2_password_authenticator::enqueue_perform_full_authentication(
    auth_packet_encoder &encoder) {
  const char full_auth_code{static_cast<char>(perform_full_authentication)};
  outbound_frames_.emplace_back(
      encoder.encode_auth_method_data(std::string_view{&full_auth_code, 1U}));
}

void caching_sha2_password_authenticator::enqueue_public_key(
    auth_packet_encoder &encoder) {
  outbound_frames_.emplace_back(
      encoder.encode_auth_method_data(get_rsa_public_key_pem()));
}

void caching_sha2_password_authenticator::enqueue_fast_auth_success(
    auth_packet_encoder &encoder) {
  static constexpr std::string_view fast_auth_code{"\x03"};
  outbound_frames_.emplace_back(
      encoder.encode_auth_method_data(fast_auth_code));
}

authentication_state
caching_sha2_password_authenticator::verify_encrypted_password(
    std::string_view encrypted_password) {
  if (std::size(encrypted_password) != get_rsa_cipher_length()) {
    phase_ = phase::failed;
    return authentication_state::failed;
  }

  try {
    const auto decrypted_password{
        decrypt_rsa_password(encrypted_password, salt_)};
    if (expected_username_ == client_username_ &&
        decrypted_password == password_) {
      phase_ = phase::succeeded;
      return authentication_state::succeeded;
    }
  } catch (const std::exception &) {
    phase_ = phase::failed;
    return authentication_state::failed;
  }

  phase_ = phase::failed;
  return authentication_state::failed;
}

authentication_state
caching_sha2_password_authenticator::verify_cleartext_password(
    std::string_view password_payload) {
  // Full auth over a secure transport: client sends a 0-terminated password
  // without RSA. Minimysql has no TLS yet; connection_is_secure() is always
  // false, so this remains a placeholder until SSL is wired up.
  if (std::empty(password_payload) || password_payload.back() != '\0') {
    phase_ = phase::failed;
    return authentication_state::failed;
  }

  const std::string_view password{
      password_payload.substr(0, std::size(password_payload) - 1U)};
  if (expected_username_ == client_username_ && password == password_) {
    phase_ = phase::succeeded;
    return authentication_state::succeeded;
  }

  phase_ = phase::failed;
  return authentication_state::failed;
}

bool caching_sha2_password_authenticator::verify_greeting_scramble(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view expected_username, std::string_view client_username,
    std::string_view client_auth_data, std::string_view salt) const {
  return expected_username == client_username &&
         client_auth_data == scramble(password_, salt);
}

} // namespace minimysql
