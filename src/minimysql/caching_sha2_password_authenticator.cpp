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

#include "minimysql/network_io_operations_fwd.hpp"

#include "opensslpp/digest_context.hpp"
#include "opensslpp/rsa_private_key.hpp"

#include "util/byte_span_fwd.hpp"

// clang-format off
// caching_sha2_password authentication flow:
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
//     |   (A) secure transport (TLS negotiated via PBS-31's SSLRequest branch)
//     |--- cleartext password (0-terminated) -->|  verify_cleartext_password()
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

// Thin std::string_view adapters over the byte-span-based
// opensslpp::digest_context API so the caching_sha2_password scramble()
// implementation reads without conversion noise. The wrapper itself stays
// strictly typed on util::byte_span / util::const_byte_span (matching
// crypto_rng / cipher_context); these helpers live here because they are
// specific to how this authenticator hashes password / salt strings.
[[nodiscard]] util::const_byte_span as_const_bytes(std::string_view data) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return {reinterpret_cast<const std::byte *>(std::data(data)),
          std::size(data)};
}

[[nodiscard]] util::byte_span as_writable_bytes(std::string &data) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return {reinterpret_cast<std::byte *>(std::data(data)), std::size(data)};
}

[[nodiscard]] std::string sha256_of(std::string_view data) {
  return opensslpp::digest_context::calculate(
      opensslpp::digest_code_type::sha256, as_const_bytes(data));
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
        private_key_{private_key_pem} {}

  static std::unique_ptr<rsa_key_pair>
  from_paths(std::string_view server_rsa_public_key_path,
             std::string_view server_rsa_private_key_path) {
    return std::make_unique<rsa_key_pair>(
        read_pem_file(server_rsa_public_key_path),
        read_pem_file(server_rsa_private_key_path));
  }

  [[nodiscard]] std::string_view public_key_pem() const noexcept {
    return public_key_pem_;
  }

  [[nodiscard]] std::size_t cipher_length() const noexcept {
    return private_key_.get_cipher_length_in_bytes();
  }

  [[nodiscard]] std::string
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  decrypt_password(std::string_view encrypted_password, std::string_view salt) {
    // OAEP recovers exactly (password_length + 1) bytes: the client sent the
    // trailing NUL of the C-string as part of the OAEP plaintext, XORed with
    // the salt repeated to cover the whole plaintext. Undo the XOR and read
    // back through the NUL terminator to isolate the password.
    auto plain_text{
        private_key_.decrypt_oaep(as_const_bytes(encrypted_password))};

    xor_with_pattern(std::span{std::data(plain_text), std::size(plain_text)},
                     salt);

    const auto password_end{plain_text.find('\0')};
    if (password_end == std::string::npos) {
      throw std::runtime_error{"decrypted password is missing a terminator"};
    }

    return plain_text.substr(0, password_end);
  }

private:
  std::string public_key_pem_;
  opensslpp::rsa_private_key private_key_;
};

caching_sha2_password_authenticator::caching_sha2_password_authenticator(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view password, std::string_view server_rsa_public_key_path,
    std::string_view server_rsa_private_key_path)
    : password_{password} {
  // One-sided configuration is a mis-configuration and fails hard. Both
  // paths empty is accepted at construction: the authenticator simply
  // leaves 'rsa_keys_' null and any subsequent caching_sha2_password
  // full-auth attempt (RSA / PEM handshake) will fail at that point. The
  // "operator must configure the key pair before running 'pull'" invariant
  // is enforced one level up, at the JSON-config layer, by
  // pbs_listener_config::validate() and pull_operation.
  const bool has_public{!std::empty(server_rsa_public_key_path)};
  const bool has_private{!std::empty(server_rsa_private_key_path)};
  if (has_public != has_private) {
    throw std::runtime_error{
        "server_rsa_public_key_path and server_rsa_private_key_path must "
        "both be set or both be empty"};
  }
  if (has_public) {
    rsa_keys_ = rsa_key_pair::from_paths(server_rsa_public_key_path,
                                         server_rsa_private_key_path);
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
    [[maybe_unused]] std::string_view client_auth_data, std::string_view salt,
    bool secure_transport, auth_packet_encoder &encoder) {
  expected_username_ = expected_username;
  client_username_ = client_username;
  salt_ = salt;
  secure_transport_ = secure_transport;
  outbound_frames_.clear();
  phase_ = phase::idle;
  // PBS has no SHA2 digest cache, so we cannot honestly claim a
  // "cache hit" for the fast-auth (0x03) shortcut. Every session is treated
  // as a first-login / cache-miss and driven through the 0x04 full-auth
  // branch (RSA on plain TCP, cleartext over TLS once TLS is wired). Real
  // Percona Server / MySQL takes the fast path only on cache hit: it caches
  // the SHA-256 double-hash of the password on the first successful full
  // auth and, on subsequent logins for the same user, verifies the greeting
  // scramble against that cached hash and answers 0x03.
  //
  // The fast-path helpers (verify_greeting_scramble(),
  // enqueue_fast_auth_success(), and the scramble() static used by both)
  // are deliberately kept alive here: once PBS grows a SHA-2 digest
  // cache, this branch should reappear, gated on a cache lookup for the
  // (username, salt) pair rather than on a live compare against the
  // plaintext password. See mtr/binlog_streaming/t/caching_sha2_full_auth
  // .test for the full-auth handshake this always-miss policy exercises.
  //
  // Note: on plain TCP, mysql clients require --get-server-public-key or
  // --server-public-key-path to satisfy 0x04; without either they refuse
  // to send credentials in the clear ("Authentication requires secure
  // connection.") - that is the client's protective behaviour, not a bug
  // here.
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
//   (request PEM) or RSA ciphertext when it already has the public key, or
//   with a cleartext password when secure_transport_ is true (i.e. the
//   network layer negotiated TLS and called
//   connection_context::mark_transport_secure() before begin_authentication).
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
  auto result{sha256_of(password)};
  const auto double_hashed_password{sha256_of(result)};

  opensslpp::digest_context ctx{opensslpp::digest_code_type::sha256};
  ctx.update(as_const_bytes(double_hashed_password));
  ctx.update(as_const_bytes(salt));

  std::string salted_triple_hashed_password(ctx.get_digest_size_in_bytes(),
                                            '\0');
  ctx.finalize(as_writable_bytes(salted_triple_hashed_password));

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
    std::string_view encrypted_password, std::string_view salt) {
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
// PBS has no mpvio / plugin VIO. We write classic-protocol frames
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
  // without RSA. Reached when the network layer upgraded the transport to
  // TLS (see PBS-31) and connection_context::mark_transport_secure() was
  // called so begin_authentication() observed secure_transport_ set.
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
