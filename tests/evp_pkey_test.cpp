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

#include <climits>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#define BOOST_TEST_MODULE EVPPkeyTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/unit_test_suite.hpp>

#include <boost/test/data/monomorphic/collection.hpp>
#include <boost/test/data/test_case.hpp>

#include <boost/test/tools/context.hpp>

#include <boost/test/tools/old/interface.hpp>

#include "opensslpp/core_error.hpp"
#include "opensslpp/crypto_rng.hpp"
#include "opensslpp/encryption_padding.hpp"
#include "opensslpp/evp_pkey.hpp"

#include "util/byte_span.hpp"
#include "util/conversion_helpers.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"

static constexpr std::uint32_t key_size_in_bits{1024U};
static constexpr std::size_t key_size_in_bytes{key_size_in_bits / CHAR_BIT};

class evp_pkey_fixture {
private:
  class fixture_data {
  public:
    fixture_data()
        : private_key_{opensslpp::evp_pkey::generate(
              opensslpp::evp_pkey::get_rsa_algorithm_name(), key_size_in_bits)},
          public_key_{private_key_.derive_public_key()} {}

    fixture_data(const fixture_data &) = delete;
    fixture_data(fixture_data &&) = delete;
    fixture_data &operator=(const fixture_data &) = delete;
    fixture_data &operator=(fixture_data &&) = delete;

    ~fixture_data() = default;

    [[nodiscard]] const opensslpp::evp_pkey &get_private_key() const noexcept {
      return private_key_;
    }
    [[nodiscard]] const opensslpp::evp_pkey &get_public_key() const noexcept {
      return public_key_;
    }

  private:
    opensslpp::evp_pkey private_key_{};
    opensslpp::evp_pkey public_key_{};
  };

public:
  evp_pkey_fixture() {
    [[maybe_unused]] const auto &initialization_guarantor{data()};
  }
  evp_pkey_fixture(const evp_pkey_fixture &) = delete;
  evp_pkey_fixture(evp_pkey_fixture &&) = delete;
  evp_pkey_fixture &operator=(const evp_pkey_fixture &) = delete;
  evp_pkey_fixture &operator=(evp_pkey_fixture &&) = delete;
  ~evp_pkey_fixture() = default;

  [[nodiscard]] static const fixture_data &data() {
    static const fixture_data data{};
    return data;
  }
};

BOOST_TEST_GLOBAL_FIXTURE(evp_pkey_fixture);

BOOST_AUTO_TEST_CASE(EVPPkeyDefaultConstruction) {
  const opensslpp::evp_pkey empty_key{};
  BOOST_CHECK(empty_key.is_empty());
}

static constexpr std::initializer_list<std::string_view>
    invalid_algorithm_names{"", "INVALID", "EC", "DSA"};
BOOST_DATA_TEST_CASE(EVPPkeyGenerateInvalidAlgorithm,
                     boost::unit_test::data::make(invalid_algorithm_names),
                     algorithm_name_sv) {
  const std::string algorithm_name{algorithm_name_sv};
  [[maybe_unused]] opensslpp::evp_pkey key{};
  BOOST_CHECK_THROW(
      key = opensslpp::evp_pkey::generate(algorithm_name, key_size_in_bits),
      std::logic_error);
}

static constexpr std::initializer_list<std::size_t> invalid_key_sizes{
    0UZ,
    1UZ,
    128UZ,
    256UZ,
    511UZ,
    static_cast<std::size_t>(
        std::numeric_limits<std::make_signed_t<std::size_t>>::max()) +
        1UZ,
    std::numeric_limits<std::size_t>::max()};
BOOST_DATA_TEST_CASE(EVPPkeyGenerateInvalidKeySize,
                     boost::unit_test::data::make(invalid_key_sizes),
                     key_size) {
  [[maybe_unused]] opensslpp::evp_pkey key{};
  BOOST_CHECK_THROW(
      key = opensslpp::evp_pkey::generate(
          opensslpp::evp_pkey::get_rsa_algorithm_name(), key_size),
      opensslpp::core_error);
}

BOOST_AUTO_TEST_CASE(EVPPkeyGetSizeInBits) {
  const auto &private_key{evp_pkey_fixture::data().get_private_key()};
  BOOST_CHECK_EQUAL(private_key.get_size_in_bits(), key_size_in_bits);
  const auto public_key{private_key.derive_public_key()};
  BOOST_CHECK_EQUAL(public_key.get_size_in_bits(), key_size_in_bits);
}

BOOST_AUTO_TEST_CASE(EVPPkeyGetSizeInBytes) {
  const auto &private_key{evp_pkey_fixture::data().get_private_key()};
  BOOST_CHECK_EQUAL(private_key.get_size_in_bytes(), key_size_in_bytes);
  const auto public_key{private_key.derive_public_key()};
  BOOST_CHECK_EQUAL(public_key.get_size_in_bytes(), key_size_in_bytes);
}

BOOST_AUTO_TEST_CASE(EVPPkeyGetMaxEncryptionMessageSize) {
  const auto &private_key{evp_pkey_fixture::data().get_private_key()};
  const auto &public_key{evp_pkey_fixture::data().get_public_key()};

  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  BOOST_CHECK_EQUAL(private_key.get_max_encryption_message_size(
                        opensslpp::encryption_padding::no),
                    128UZ);
  BOOST_CHECK_EQUAL(private_key.get_max_encryption_message_size(
                        opensslpp::encryption_padding::pkcs1),
                    117UZ);
  BOOST_CHECK_EQUAL(private_key.get_max_encryption_message_size(
                        opensslpp::encryption_padding::pkcs1_oaep),
                    86UZ);

  BOOST_CHECK_EQUAL(public_key.get_max_encryption_message_size(
                        opensslpp::encryption_padding::no),
                    128UZ);
  BOOST_CHECK_EQUAL(public_key.get_max_encryption_message_size(
                        opensslpp::encryption_padding::pkcs1),
                    117UZ);
  BOOST_CHECK_EQUAL(public_key.get_max_encryption_message_size(
                        opensslpp::encryption_padding::pkcs1_oaep),
                    86UZ);
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
}

BOOST_AUTO_TEST_CASE(EVPPkeyGeneratedPrivateKey) {
  const auto &private_key{evp_pkey_fixture::data().get_private_key()};
  BOOST_REQUIRE(!private_key.is_empty());
  BOOST_CHECK(private_key.has_private_component());
}

BOOST_AUTO_TEST_CASE(EVPPkeyDerivedPublicKey) {
  const auto &public_key{evp_pkey_fixture::data().get_public_key()};
  BOOST_REQUIRE(!public_key.is_empty());
  BOOST_CHECK(!public_key.has_private_component());
}

BOOST_AUTO_TEST_CASE(EVPPkeyPrivatePEMRoundTrip) {
  [[maybe_unused]] opensslpp::evp_pkey key{};

  const auto &private_key{evp_pkey_fixture::data().get_private_key()};
  const auto private_pem{opensslpp::evp_pkey::export_private_pem(private_key)};
  const auto public_pem{opensslpp::evp_pkey::export_public_pem(private_key)};

  const auto imported_private_key{
      opensslpp::evp_pkey::import_private_pem(private_pem)};
  BOOST_CHECK_THROW(key = opensslpp::evp_pkey::import_public_pem(private_pem),
                    opensslpp::core_error);
  BOOST_REQUIRE(!imported_private_key.is_empty());
  BOOST_CHECK(imported_private_key.has_private_component());
  BOOST_CHECK_EQUAL(imported_private_key.get_size_in_bits(), key_size_in_bits);
  BOOST_CHECK_EQUAL(imported_private_key.get_size_in_bytes(),
                    key_size_in_bytes);

  const auto imported_public_key{
      opensslpp::evp_pkey::import_public_pem(public_pem)};
  BOOST_CHECK_THROW(key = opensslpp::evp_pkey::import_private_pem(public_pem),
                    opensslpp::core_error);
  BOOST_REQUIRE(!imported_public_key.is_empty());
  BOOST_CHECK(!imported_public_key.has_private_component());
  BOOST_CHECK_EQUAL(imported_public_key.get_size_in_bits(), key_size_in_bits);
  BOOST_CHECK_EQUAL(imported_public_key.get_size_in_bytes(), key_size_in_bytes);

  BOOST_CHECK_EQUAL(
      opensslpp::evp_pkey::export_private_pem(imported_private_key),
      private_pem);
  BOOST_CHECK_EQUAL(opensslpp::evp_pkey::export_public_pem(imported_public_key),
                    public_pem);
}

BOOST_AUTO_TEST_CASE(EVPPkeyPublicPEMRoundTrip) {
  [[maybe_unused]] std::string pem{};
  [[maybe_unused]] opensslpp::evp_pkey key{};

  const auto &public_key{evp_pkey_fixture::data().get_public_key()};
  BOOST_CHECK_THROW(pem = opensslpp::evp_pkey::export_private_pem(public_key),
                    opensslpp::core_error);
  const auto public_pem{opensslpp::evp_pkey::export_public_pem(public_key)};

  BOOST_CHECK_THROW(key = opensslpp::evp_pkey::import_private_pem(public_pem),
                    opensslpp::core_error);
  const auto imported_public_key{
      opensslpp::evp_pkey::import_public_pem(public_pem)};
  BOOST_REQUIRE(!imported_public_key.is_empty());
  BOOST_CHECK(!imported_public_key.has_private_component());
  BOOST_CHECK_EQUAL(imported_public_key.get_size_in_bits(), key_size_in_bits);
  BOOST_CHECK_EQUAL(imported_public_key.get_size_in_bytes(), key_size_in_bytes);

  BOOST_CHECK_EQUAL(opensslpp::evp_pkey::export_public_pem(imported_public_key),
                    public_pem);
}

BOOST_AUTO_TEST_CASE(EVPPkeyEncryptDecryptRoundTripNoPadding) {
  const auto &private_key{evp_pkey_fixture::data().get_private_key()};
  const auto &public_key{evp_pkey_fixture::data().get_public_key()};
  constexpr auto padding{opensslpp::encryption_padding::no};

  util::dynamic_byte_buffer message(key_size_in_bytes);
  opensslpp::crypto_rng::generate(message);
  // clearing the most significant byte here to make sure the message
  // is smaller than the modulus (to avoid the 'data too large for modulus'
  // errors generated by OpenSSL)
  message.front() = std::byte{};

  const auto encrypted{
      opensslpp::evp_key_encrypt(message, public_key, padding)};
  BOOST_REQUIRE_EQUAL(std::size(encrypted), key_size_in_bytes);

  const auto encrypted_irregular{
      opensslpp::evp_key_encrypt(message, private_key, padding)};
  BOOST_REQUIRE_EQUAL(std::size(encrypted_irregular), key_size_in_bytes);
  BOOST_CHECK(encrypted == encrypted_irregular);

  util::dynamic_byte_buffer buffer{};
  BOOST_CHECK_THROW(
      buffer = opensslpp::evp_key_decrypt(encrypted, public_key, padding),
      opensslpp::core_error);
  const auto decrypted{
      opensslpp::evp_key_decrypt(encrypted, private_key, padding)};
  BOOST_CHECK(decrypted == message);

  BOOST_CHECK_THROW(buffer = opensslpp::evp_key_decrypt(encrypted_irregular,
                                                        public_key, padding),
                    opensslpp::core_error);
  const auto decrypted_irregular{
      opensslpp::evp_key_decrypt(encrypted_irregular, private_key, padding)};
  BOOST_CHECK(decrypted_irregular == message);
}

static constexpr std::initializer_list<std::size_t>
    invalid_no_padding_message_sizes{0UZ, 1UZ, key_size_in_bytes - 1UZ,
                                     key_size_in_bytes + 1UZ};
BOOST_DATA_TEST_CASE(
    EVPPkeyEncryptDecryptWrongMessageSizeNoPadding,
    boost::unit_test::data::make(invalid_no_padding_message_sizes),
    message_size) {
  const auto &private_key{evp_pkey_fixture::data().get_private_key()};
  const auto &public_key{evp_pkey_fixture::data().get_public_key()};
  constexpr auto padding{opensslpp::encryption_padding::no};

  util::dynamic_byte_buffer message(message_size);

  util::dynamic_byte_buffer buffer{};
  BOOST_CHECK_THROW(
      buffer = opensslpp::evp_key_encrypt(message, public_key, padding),
      opensslpp::core_error);
  BOOST_CHECK_THROW(
      buffer = opensslpp::evp_key_decrypt(message, private_key, padding),
      opensslpp::core_error);
}

static constexpr std::initializer_list<
    std::underlying_type_t<opensslpp::encryption_padding>>
    encryption_paddings{
        std::to_underlying(opensslpp::encryption_padding::pkcs1),
        std::to_underlying(opensslpp::encryption_padding::pkcs1_oaep)};

// the cognitive complexity of the following two test cases crosses the
// limit only because of extensive macro usage - from the user perspective
// these functions do not look complex at all

// NOLINTBEGIN(readability-function-cognitive-complexity)
BOOST_DATA_TEST_CASE(EVPPkeyEncryptDecryptRoundTripWithPadding,
                     boost::unit_test::data::make(encryption_paddings),
                     padding_raw) {
  const auto &private_key{evp_pkey_fixture::data().get_private_key()};
  const auto &public_key{evp_pkey_fixture::data().get_public_key()};
  const auto padding{
      util::from_underlying<opensslpp::encryption_padding>(padding_raw)};

  const auto max_size{public_key.get_max_encryption_message_size(padding)};
  const std::initializer_list<std::size_t> message_sizes{
      0UZ, 1UZ, max_size - 1UZ, max_size};
  for (const auto message_size : message_sizes) {
    BOOST_TEST_CONTEXT("message_size = " << message_size) {
      util::dynamic_byte_buffer message(message_size);
      opensslpp::crypto_rng::generate(message);

      const auto encrypted{
          opensslpp::evp_key_encrypt(message, public_key, padding)};
      BOOST_REQUIRE_EQUAL(std::size(encrypted), key_size_in_bytes);

      const auto encrypted_irregular{
          opensslpp::evp_key_encrypt(message, private_key, padding)};
      BOOST_REQUIRE_EQUAL(std::size(encrypted_irregular), key_size_in_bytes);
      // encrypting the same message twice with the same key should yield
      // different ciphertexts when (randomized) padding is used
      BOOST_CHECK(encrypted != encrypted_irregular);

      util::dynamic_byte_buffer buffer{};
      const auto decrypted{
          opensslpp::evp_key_decrypt(encrypted, private_key, padding)};
      BOOST_CHECK(decrypted == message);

      BOOST_CHECK_THROW(buffer = opensslpp::evp_key_decrypt(
                            encrypted_irregular, public_key, padding),
                        opensslpp::core_error);
      const auto decrypted_irregular{opensslpp::evp_key_decrypt(
          encrypted_irregular, private_key, padding)};
      BOOST_CHECK(decrypted_irregular == message);
    }
  }
}

BOOST_DATA_TEST_CASE(EVPPkeyEncryptDecryptWrongMessageSizeWithPadding,
                     boost::unit_test::data::make(encryption_paddings),
                     padding_raw) {
  const auto &private_key{evp_pkey_fixture::data().get_private_key()};
  const auto &public_key{evp_pkey_fixture::data().get_public_key()};
  const auto padding{
      util::from_underlying<opensslpp::encryption_padding>(padding_raw)};

  const auto max_size{public_key.get_max_encryption_message_size(padding)};

  const std::initializer_list<std::size_t> invalid_encryption_message_sizes{
      max_size + 1UZ, max_size * 2UZ};
  for (const auto message_size : invalid_encryption_message_sizes) {
    BOOST_TEST_CONTEXT("encryption message_size = " << message_size) {
      util::dynamic_byte_buffer message(message_size);

      util::dynamic_byte_buffer buffer{};
      BOOST_CHECK_THROW(
          buffer = opensslpp::evp_key_encrypt(message, public_key, padding),
          opensslpp::core_error);
    }
  }

  const std::initializer_list<std::size_t> invalid_decryption_message_sizes{
      0UZ, 1UZ, max_size, key_size_in_bytes - 1UZ, key_size_in_bytes + 1UZ};
  for (const auto message_size : invalid_decryption_message_sizes) {
    BOOST_TEST_CONTEXT("decryption message_size = " << message_size) {
      util::dynamic_byte_buffer message(message_size);

      util::dynamic_byte_buffer buffer{};
      BOOST_CHECK_THROW(
          buffer = opensslpp::evp_key_decrypt(message, private_key, padding),
          opensslpp::core_error);
    }
  }
}
// NOLINTEND(readability-function-cognitive-complexity)

BOOST_AUTO_TEST_CASE(EVPPkeyImportInvalidPEM) {
  [[maybe_unused]] opensslpp::evp_pkey imported_key{};
  BOOST_CHECK_THROW(imported_key =
                        opensslpp::evp_pkey::import_private_pem("invalid PEM"),
                    opensslpp::core_error);
  BOOST_CHECK_THROW(imported_key =
                        opensslpp::evp_pkey::import_public_pem("invalid PEM"),
                    opensslpp::core_error);
}
