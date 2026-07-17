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

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#define BOOST_TEST_MODULE CachingSha2PasswordAuthenticatorTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test_suite.hpp>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/types.h>

#include "minimysql/caching_sha2_password_authenticator.hpp"
#include "minimysql/network_io_operations_fwd.hpp"

namespace {

class recording_encoder final : public minimysql::auth_packet_encoder {
public:
  [[nodiscard]] minimysql::network_buffer_type
  encode_single_byte(std::uint8_t payload_byte) override {
    minimysql::network_buffer_type frame;
    frame.push_back(static_cast<char>(payload_byte));
    return frame;
  }

  [[nodiscard]] minimysql::network_buffer_type
  encode_raw(std::string_view payload) override {
    return minimysql::network_buffer_type{payload};
  }

  [[nodiscard]] minimysql::network_buffer_type
  encode_auth_method_data(std::string_view payload) override {
    minimysql::network_buffer_type frame;
    frame.push_back('\x01');
    frame.append(payload);
    return frame;
  }

  void validate_incoming_sequence(
      const minimysql::network_buffer_type & /*payload*/) override {}

  [[nodiscard]] std::string_view
  frame_payload(const minimysql::network_buffer_type &payload) const override {
    return payload;
  }
};

[[nodiscard]] std::string_view auth_more_data_payload(std::string_view frame) {
  BOOST_REQUIRE_GE(std::size(frame), 1U);
  BOOST_REQUIRE_EQUAL(static_cast<unsigned char>(frame.front()), 0x01U);
  return frame.substr(1U);
}

[[nodiscard]] std::string rsa_encrypt_password(std::string_view public_key_pem,
                                               std::string_view password,
                                               std::string_view salt) {
  BIO *bio{BIO_new_mem_buf(std::data(public_key_pem),
                           static_cast<int>(std::size(public_key_pem)))};
  BOOST_REQUIRE(bio != nullptr);

  EVP_PKEY *key{PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr)};
  BIO_free(bio);
  BOOST_REQUIRE(key != nullptr);

  const std::size_t cipher_length{
      static_cast<std::size_t>(EVP_PKEY_get_size(key))};
  std::string plain(std::size(password) + 1U, '\0');
  plain.replace(0, std::size(password), password);

  for (std::size_t index{0U}; index < std::size(plain); ++index) {
    plain[index] = static_cast<char>(
        static_cast<unsigned char>(plain[index]) ^
        static_cast<unsigned char>(salt[index % std::size(salt)]));
  }

  std::string cipher(cipher_length, '\0');
  std::size_t out_length{cipher_length};

  EVP_PKEY_CTX *ctx{EVP_PKEY_CTX_new(key, nullptr)};
  BOOST_REQUIRE(ctx != nullptr);
  BOOST_REQUIRE(EVP_PKEY_encrypt_init(ctx) > 0);
  BOOST_REQUIRE(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) > 0);
  BOOST_REQUIRE(
      EVP_PKEY_encrypt(
          ctx,
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<unsigned char *>(std::data(cipher)), &out_length,
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<const unsigned char *>(std::data(plain)),
          std::size(plain)) > 0);

  EVP_PKEY_CTX_free(ctx);
  EVP_PKEY_free(key);
  cipher.resize(out_length);
  return cipher;
}

void write_temp_file(const std::string &path, std::string_view contents) {
  std::ofstream out{path, std::ios::binary | std::ios::trunc};
  BOOST_REQUIRE(out);
  out.write(std::data(contents),
            static_cast<std::streamsize>(std::size(contents)));
  BOOST_REQUIRE(out);
}

} // namespace

BOOST_AUTO_TEST_CASE(OneSidedServerRsaPathsThrow) {
  BOOST_CHECK_THROW(minimysql::caching_sha2_password_authenticator(
                        "password", "/only/pub.pem", ""),
                    std::runtime_error);
  BOOST_CHECK_THROW(minimysql::caching_sha2_password_authenticator(
                        "password", "", "/only/priv.pem"),
                    std::runtime_error);
}

BOOST_AUTO_TEST_CASE(EmptyServerRsaPathsUseEmbeddedDefaults) {
  BOOST_CHECK_NO_THROW(
      minimysql::caching_sha2_password_authenticator("password"));
}

BOOST_AUTO_TEST_CASE(FastAuthPathSucceedsWithMatchingScramble) {
  static constexpr std::string_view password{"password"};
  static constexpr std::string_view username{"rpl"};
  static constexpr std::string_view salt{"01234567890123456789"};

  minimysql::caching_sha2_password_authenticator authenticator{password};
  recording_encoder encoder;

  const auto scramble{
      minimysql::caching_sha2_password_authenticator::scramble(password, salt)};
  authenticator.begin_authentication(username, username, scramble, salt, false,
                                     encoder);

  const auto outbound{authenticator.take_outbound_frames()};
  BOOST_REQUIRE_EQUAL(std::size(outbound), 1U);
  BOOST_CHECK_EQUAL(auth_more_data_payload(outbound.front()),
                    std::string_view{"\x03"});
  BOOST_CHECK(authenticator.state() ==
              minimysql::authentication_state::succeeded);
  BOOST_CHECK(!authenticator.expects_client_input());
}

BOOST_AUTO_TEST_CASE(FullAuthRsaPathSucceedsViaPublicKeyRequest) {
  static constexpr std::string_view password{"password"};
  static constexpr std::string_view username{"rpl"};
  static constexpr std::string_view salt{"01234567890123456789"};

  minimysql::caching_sha2_password_authenticator authenticator{password};
  recording_encoder encoder;

  authenticator.begin_authentication(username, username, "bad-scramble", salt,
                                     false, encoder);

  auto outbound{authenticator.take_outbound_frames()};
  BOOST_REQUIRE_EQUAL(std::size(outbound), 1U);
  BOOST_CHECK_EQUAL(auth_more_data_payload(outbound.front()),
                    std::string_view{"\x04"});
  BOOST_CHECK(authenticator.state() ==
              minimysql::authentication_state::in_progress);

  const minimysql::network_buffer_type public_key_request{"\x02"};
  BOOST_CHECK(authenticator.submit_client_frame(public_key_request, encoder) ==
              minimysql::authentication_state::in_progress);

  outbound = authenticator.take_outbound_frames();
  BOOST_REQUIRE_EQUAL(std::size(outbound), 1U);
  const auto public_key_pem{auth_more_data_payload(outbound.front())};
  BOOST_CHECK(public_key_pem.starts_with("-----BEGIN PUBLIC KEY-----"));

  const auto ciphertext{rsa_encrypt_password(public_key_pem, password, salt)};
  BOOST_CHECK(authenticator.submit_client_frame(ciphertext, encoder) ==
              minimysql::authentication_state::succeeded);
  BOOST_CHECK(authenticator.state() ==
              minimysql::authentication_state::succeeded);
}

BOOST_AUTO_TEST_CASE(FullAuthCleartextPathOnSecureTransport) {
  static constexpr std::string_view password{"password"};
  static constexpr std::string_view username{"rpl"};
  static constexpr std::string_view salt{"01234567890123456789"};

  minimysql::caching_sha2_password_authenticator authenticator{password};
  recording_encoder encoder;

  authenticator.begin_authentication(username, username, "bad-scramble", salt,
                                     true, encoder);
  (void)authenticator.take_outbound_frames();

  minimysql::network_buffer_type cleartext{password};
  cleartext.push_back('\0');
  BOOST_CHECK(authenticator.submit_client_frame(cleartext, encoder) ==
              minimysql::authentication_state::succeeded);
}

BOOST_AUTO_TEST_CASE(BothServerRsaPathsLoadSuccessfully) {
  const std::string pub_path{"/tmp/minimysql_test_server_rsa_public.pem"};
  const std::string priv_path{"/tmp/minimysql_test_server_rsa_private.pem"};

  EVP_PKEY_CTX *ctx{EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr)};
  BOOST_REQUIRE(ctx != nullptr);
  BOOST_REQUIRE(EVP_PKEY_keygen_init(ctx) > 0);
  BOOST_REQUIRE(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) > 0);
  EVP_PKEY *key{nullptr};
  BOOST_REQUIRE(EVP_PKEY_keygen(ctx, &key) > 0);
  EVP_PKEY_CTX_free(ctx);

  BIO *pub_bio{BIO_new(BIO_s_mem())};
  BIO *priv_bio{BIO_new(BIO_s_mem())};
  BOOST_REQUIRE(pub_bio != nullptr);
  BOOST_REQUIRE(priv_bio != nullptr);
  BOOST_REQUIRE(PEM_write_bio_PUBKEY(pub_bio, key) == 1);
  BOOST_REQUIRE(PEM_write_bio_PrivateKey(priv_bio, key, nullptr, nullptr, 0,
                                         nullptr, nullptr) == 1);

  char *pub_data{nullptr};
  char *priv_data{nullptr};
  const long pub_len{BIO_get_mem_data(pub_bio, &pub_data)};
  const long priv_len{BIO_get_mem_data(priv_bio, &priv_data)};
  BOOST_REQUIRE(pub_len > 0);
  BOOST_REQUIRE(priv_len > 0);

  write_temp_file(
      pub_path, std::string_view{pub_data, static_cast<std::size_t>(pub_len)});
  write_temp_file(
      priv_path,
      std::string_view{priv_data, static_cast<std::size_t>(priv_len)});

  BIO_free(pub_bio);
  BIO_free(priv_bio);
  EVP_PKEY_free(key);

  BOOST_CHECK_NO_THROW(minimysql::caching_sha2_password_authenticator(
      "password", pub_path, priv_path));
}
