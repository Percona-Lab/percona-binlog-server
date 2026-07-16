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

#ifndef MINIMYSQL_CACHING_SHA2_PASSWORD_AUTHENTICATOR_HPP
#define MINIMYSQL_CACHING_SHA2_PASSWORD_AUTHENTICATOR_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "minimysql/network_io_operations_fwd.hpp"

namespace minimysql {

class auth_packet_encoder {
public:
  auth_packet_encoder() = default;
  virtual ~auth_packet_encoder() = default;

  auth_packet_encoder(const auth_packet_encoder &) = delete;
  auth_packet_encoder(auth_packet_encoder &&) = delete;
  auth_packet_encoder &operator=(const auth_packet_encoder &) = delete;
  auth_packet_encoder &operator=(auth_packet_encoder &&) = delete;

  [[nodiscard]] virtual network_buffer_type
  encode_single_byte(std::uint8_t payload_byte) = 0;
  [[nodiscard]] virtual network_buffer_type
  encode_raw(std::string_view payload) = 0;
  [[nodiscard]] virtual network_buffer_type
  encode_auth_method_data(std::string_view payload) = 0;
  virtual void
  validate_incoming_sequence(const network_buffer_type &payload) = 0;
  [[nodiscard]] virtual std::string_view
  frame_payload(const network_buffer_type &payload) const = 0;
};

enum class authentication_state : std::uint8_t {
  in_progress,
  succeeded,
  failed,
};

class caching_sha2_password_authenticator {
public:
  static constexpr std::string_view plugin_name{"caching_sha2_password"};

  explicit caching_sha2_password_authenticator(
      std::string_view password,
      std::string_view server_rsa_public_key_path = {},
      std::string_view server_rsa_private_key_path = {});
  ~caching_sha2_password_authenticator();

  caching_sha2_password_authenticator(
      const caching_sha2_password_authenticator &) = delete;
  caching_sha2_password_authenticator &
  operator=(const caching_sha2_password_authenticator &) = delete;
  caching_sha2_password_authenticator(
      caching_sha2_password_authenticator &&) noexcept = default;
  caching_sha2_password_authenticator &
  operator=(caching_sha2_password_authenticator &&) noexcept = default;

  [[nodiscard]] static bool
  needs_auth_method_switch(std::string_view client_plugin) noexcept;

  [[nodiscard]] static std::string
  generate_auth_switch_plugin_data(std::string_view salt);

  void begin_authentication(std::string_view expected_username,
                            std::string_view client_username,
                            std::string_view client_auth_data,
                            std::string_view salt, bool secure_transport,
                            auth_packet_encoder &encoder);

  [[nodiscard]] authentication_state state() const noexcept;

  [[nodiscard]] bool expects_client_input() const noexcept;

  [[nodiscard]] std::vector<network_buffer_type> take_outbound_frames();

  authentication_state submit_client_frame(const network_buffer_type &frame,
                                           auth_packet_encoder &encoder);

  [[nodiscard]] static std::string scramble(std::string_view password,
                                            std::string_view salt);

private:
  struct rsa_key_pair;

  [[nodiscard]] std::string_view get_rsa_public_key_pem() const noexcept;
  [[nodiscard]] std::size_t get_rsa_cipher_length() const noexcept;
  [[nodiscard]] static bool
  check_public_key_request(std::string_view payload) noexcept;
  [[nodiscard]] std::string
  decrypt_rsa_password(std::string_view encrypted_password,
                       std::string_view salt) const;

  void enqueue_perform_full_authentication(auth_packet_encoder &encoder);
  void enqueue_public_key(auth_packet_encoder &encoder);
  void enqueue_fast_auth_success(auth_packet_encoder &encoder);
  [[nodiscard]] authentication_state
  verify_encrypted_password(std::string_view encrypted_password);
  [[nodiscard]] authentication_state
  verify_cleartext_password(std::string_view password_payload);

  [[nodiscard]] bool verify_greeting_scramble(
      std::string_view expected_username, std::string_view client_username,
      std::string_view client_auth_data, std::string_view salt) const;

  std::string password_;
  std::string expected_username_;
  std::string client_username_;
  std::string salt_;
  bool secure_transport_{false};
  std::unique_ptr<rsa_key_pair> rsa_keys_;

  enum class phase : std::uint8_t {
    idle,
    awaiting_full_auth_response,
    awaiting_encrypted_password,
    succeeded,
    failed,
  };
  phase phase_{phase::idle};
  std::vector<network_buffer_type> outbound_frames_;
};

} // namespace minimysql

#endif // MINIMYSQL_CACHING_SHA2_PASSWORD_AUTHENTICATOR_HPP
