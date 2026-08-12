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

#include <cstddef>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#define BOOST_TEST_MODULE CipherContextTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/unit_test_suite.hpp>

#include <boost/test/data/test_case.hpp>

#include <boost/test/data/monomorphic/collection.hpp>

#include <boost/test/tools/old/interface.hpp>

#include "opensslpp/cipher_context.hpp"
#include "opensslpp/cipher_mode_type.hpp"
#include "opensslpp/core_error.hpp"
#include "opensslpp/crypto_rng.hpp"

#include "util/byte_span.hpp"

using buffer_type = std::vector<std::byte>;
static const char *const invalid_cipher_name{"INVALID-256-BBB"};
static const char *const unsupported_cipher_name{"AES-128-CFB"};

BOOST_AUTO_TEST_CASE(CipherContextDefaultConstruction) {
  const opensslpp::cipher_context empty_ctx{};
  BOOST_CHECK(empty_ctx.is_empty());
}

BOOST_AUTO_TEST_CASE(CipherContextValidCipherNameConstruction) {
  const std::string cipher_name{"AES-256-CBC"};
  buffer_type key{
      opensslpp::cipher_context::get_key_size_in_bytes(cipher_name)};
  buffer_type ivec{
      opensslpp::cipher_context::get_iv_size_in_bytes(cipher_name)};
  opensslpp::crypto_rng::generate(key);
  opensslpp::crypto_rng::generate(ivec);

  const opensslpp::cipher_context empty_ctx(
      opensslpp::cipher_context_operation_type::encryption, cipher_name, key,
      ivec);
  BOOST_CHECK(!empty_ctx.is_empty());
}

BOOST_AUTO_TEST_CASE(CipherContextInvalidCipherNameConstruction) {
  static constexpr std::size_t default_key_size{16U};
  static constexpr std::size_t default_ivec_size{16U};
  buffer_type key(default_key_size);
  buffer_type ivec(default_ivec_size);
  opensslpp::crypto_rng::generate(key);
  opensslpp::crypto_rng::generate(ivec);
  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::encryption,
                        invalid_cipher_name, key, ivec),
                    opensslpp::core_error);
}

BOOST_AUTO_TEST_CASE(CipherContextUnsupportedCipherModeConstruction) {
  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::encryption,
                        unsupported_cipher_name, util::const_byte_span{},
                        util::const_byte_span{}),
                    opensslpp::core_error);
}

static const std::initializer_list<const char *> modes{"ECB", "CBC", "CTR",
                                                       "GCM"};
static const std::initializer_list<std::size_t> bit_lengths{128U, 192U, 256U};

static constexpr std::size_t zeroing_modifier{
    std::numeric_limits<std::size_t>::max()};
static const std::initializer_list<std::size_t> size_modifiers{
    0U, 1U, 16U, zeroing_modifier};

BOOST_DATA_TEST_CASE(CipherContextInvalidKeyLengthIVLengthConstruction,
                     boost::unit_test::data::make(modes) *
                         boost::unit_test::data::make(bit_lengths) *
                         boost::unit_test::data::make(size_modifiers) *
                         boost::unit_test::data::make(size_modifiers),
                     mode, bit_length, key_size_modifier, ivec_size_modifier) {
  const std::string cipher_name{"AES-" + std::to_string(bit_length) + "-" +
                                mode};

  const auto valid_key_size{
      opensslpp::cipher_context::get_key_size_in_bytes(cipher_name)};
  const auto valid_ivec_size{
      opensslpp::cipher_context::get_iv_size_in_bytes(cipher_name)};

  const auto length_adjuster{
      [](std::size_t length, std::size_t modifier) -> std::size_t {
        if (modifier == zeroing_modifier) {
          return 0U;
        }
        return length + modifier;
      }};

  const auto adjusted_key_size{
      length_adjuster(valid_key_size, key_size_modifier)};
  const auto adjusted_ivec_size{
      length_adjuster(valid_ivec_size, ivec_size_modifier)};

  if (adjusted_key_size == valid_key_size &&
      adjusted_ivec_size == valid_ivec_size) {
    // this combination is valid, so we skip it
    return;
  }

  buffer_type key{adjusted_key_size};
  buffer_type ivec{adjusted_ivec_size};
  opensslpp::crypto_rng::generate(key);
  opensslpp::crypto_rng::generate(ivec);

  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::encryption,
                        cipher_name, key, ivec),
                    opensslpp::core_error);
}

BOOST_AUTO_TEST_CASE(CipherContextGetModeStatic) {
  BOOST_CHECK(opensslpp::cipher_context::get_mode(invalid_cipher_name) ==
              opensslpp::cipher_mode_type::delimiter);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-128-ECB") ==
              opensslpp::cipher_mode_type::ecb);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-192-ECB") ==
              opensslpp::cipher_mode_type::ecb);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-256-ECB") ==
              opensslpp::cipher_mode_type::ecb);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-128-CBC") ==
              opensslpp::cipher_mode_type::cbc);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-192-CBC") ==
              opensslpp::cipher_mode_type::cbc);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-256-CBC") ==
              opensslpp::cipher_mode_type::cbc);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-128-CTR") ==
              opensslpp::cipher_mode_type::ctr);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-192-CTR") ==
              opensslpp::cipher_mode_type::ctr);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-256-CTR") ==
              opensslpp::cipher_mode_type::ctr);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-128-GCM") ==
              opensslpp::cipher_mode_type::gcm);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-192-GCM") ==
              opensslpp::cipher_mode_type::gcm);
  BOOST_CHECK(opensslpp::cipher_context::get_mode("AES-256-GCM") ==
              opensslpp::cipher_mode_type::gcm);
}

BOOST_AUTO_TEST_CASE(CipherContextGetBlockSizeStatic) {
  [[maybe_unused]] std::size_t ivec_size{};
  BOOST_CHECK_THROW(ivec_size = opensslpp::cipher_context::get_iv_size_in_bytes(
                        invalid_cipher_name),
                    opensslpp::core_error);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-128-ECB") == 16U);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-192-ECB") == 16U);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-256-ECB") == 16U);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-128-CBC") == 16U);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-192-CBC") == 16U);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-256-CBC") == 16U);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-128-CTR") == 1U);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-192-CTR") == 1U);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-256-CTR") == 1U);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-128-GCM") == 1U);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-192-GCM") == 1U);
  BOOST_CHECK(
      opensslpp::cipher_context::get_block_size_in_bytes("AES-256-GCM") == 1U);
}

BOOST_AUTO_TEST_CASE(CipherContextGetKeySizeStatic) {
  [[maybe_unused]] std::size_t key_size{};
  BOOST_CHECK_THROW(key_size = opensslpp::cipher_context::get_key_size_in_bytes(
                        invalid_cipher_name),
                    opensslpp::core_error);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-128-ECB") ==
              16U);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-192-ECB") ==
              24U);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-256-ECB") ==
              32U);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-128-CBC") ==
              16U);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-192-CBC") ==
              24U);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-256-CBC") ==
              32U);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-128-CTR") ==
              16U);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-192-CTR") ==
              24U);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-256-CTR") ==
              32U);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-128-GCM") ==
              16U);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-192-GCM") ==
              24U);
  BOOST_CHECK(opensslpp::cipher_context::get_key_size_in_bytes("AES-256-GCM") ==
              32U);
}

BOOST_AUTO_TEST_CASE(CipherContextGetIVSizeStatic) {
  [[maybe_unused]] std::size_t ivec_size{};
  BOOST_CHECK_THROW(ivec_size = opensslpp::cipher_context::get_iv_size_in_bytes(
                        invalid_cipher_name),
                    opensslpp::core_error);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-128-ECB") ==
              0U);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-192-ECB") ==
              0U);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-256-ECB") ==
              0U);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-128-CBC") ==
              16U);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-192-CBC") ==
              16U);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-256-CBC") ==
              16U);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-128-CTR") ==
              16U);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-192-CTR") ==
              16U);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-256-CTR") ==
              16U);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-128-GCM") ==
              12U);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-192-GCM") ==
              12U);
  BOOST_CHECK(opensslpp::cipher_context::get_iv_size_in_bytes("AES-256-GCM") ==
              12U);
}

class cipher_context_fixture {
protected:
  auto create_encryption_context(opensslpp::cipher_context_operation_type mode,
                                 const std::string &cipher_name) {
    key_.resize(opensslpp::cipher_context::get_key_size_in_bytes(cipher_name));
    ivec_.resize(opensslpp::cipher_context::get_iv_size_in_bytes(cipher_name));
    opensslpp::crypto_rng::generate(key_);
    opensslpp::crypto_rng::generate(ivec_);
    return opensslpp::cipher_context(mode, cipher_name, key_, ivec_);
  }
  auto create_encryption_context(const std::string &cipher_name) {
    return create_encryption_context(
        opensslpp::cipher_context_operation_type::encryption, cipher_name);
  }
  auto create_decryption_context(const std::string &cipher_name) {
    return create_encryption_context(
        opensslpp::cipher_context_operation_type::decryption, cipher_name);
  }

private:
  buffer_type key_;
  buffer_type ivec_;
};

BOOST_FIXTURE_TEST_CASE(CipherContextGetOperation, cipher_context_fixture) {
  auto encryption_context{create_encryption_context("AES-128-ECB")};
  BOOST_CHECK(encryption_context.get_operation() ==
              opensslpp::cipher_context_operation_type::encryption);
  auto decryption_context{create_decryption_context("AES-128-ECB")};
  BOOST_CHECK(decryption_context.get_operation() ==
              opensslpp::cipher_context_operation_type::decryption);
}

BOOST_FIXTURE_TEST_CASE(CipherContextGetMode, cipher_context_fixture) {
  BOOST_CHECK(create_encryption_context("AES-128-ECB").get_mode() ==
              opensslpp::cipher_mode_type::ecb);
  BOOST_CHECK(create_encryption_context("AES-192-ECB").get_mode() ==
              opensslpp::cipher_mode_type::ecb);
  BOOST_CHECK(create_encryption_context("AES-256-ECB").get_mode() ==
              opensslpp::cipher_mode_type::ecb);
  BOOST_CHECK(create_encryption_context("AES-128-CBC").get_mode() ==
              opensslpp::cipher_mode_type::cbc);
  BOOST_CHECK(create_encryption_context("AES-192-CBC").get_mode() ==
              opensslpp::cipher_mode_type::cbc);
  BOOST_CHECK(create_encryption_context("AES-256-CBC").get_mode() ==
              opensslpp::cipher_mode_type::cbc);
  BOOST_CHECK(create_encryption_context("AES-128-CTR").get_mode() ==
              opensslpp::cipher_mode_type::ctr);
  BOOST_CHECK(create_encryption_context("AES-192-CTR").get_mode() ==
              opensslpp::cipher_mode_type::ctr);
  BOOST_CHECK(create_encryption_context("AES-256-CTR").get_mode() ==
              opensslpp::cipher_mode_type::ctr);
  BOOST_CHECK(create_encryption_context("AES-128-GCM").get_mode() ==
              opensslpp::cipher_mode_type::gcm);
  BOOST_CHECK(create_encryption_context("AES-192-GCM").get_mode() ==
              opensslpp::cipher_mode_type::gcm);
  BOOST_CHECK(create_encryption_context("AES-256-GCM").get_mode() ==
              opensslpp::cipher_mode_type::gcm);
}

BOOST_FIXTURE_TEST_CASE(CipherContextGetBlockSize, cipher_context_fixture) {
  BOOST_CHECK(
      create_encryption_context("AES-128-ECB").get_block_size_in_bytes() ==
      16U);
  BOOST_CHECK(
      create_encryption_context("AES-192-ECB").get_block_size_in_bytes() ==
      16U);
  BOOST_CHECK(
      create_encryption_context("AES-256-ECB").get_block_size_in_bytes() ==
      16U);
  BOOST_CHECK(
      create_encryption_context("AES-128-CBC").get_block_size_in_bytes() ==
      16U);
  BOOST_CHECK(
      create_encryption_context("AES-192-CBC").get_block_size_in_bytes() ==
      16U);
  BOOST_CHECK(
      create_encryption_context("AES-256-CBC").get_block_size_in_bytes() ==
      16U);
  BOOST_CHECK(
      create_encryption_context("AES-128-CTR").get_block_size_in_bytes() == 1U);
  BOOST_CHECK(
      create_encryption_context("AES-192-CTR").get_block_size_in_bytes() == 1U);
  BOOST_CHECK(
      create_encryption_context("AES-256-CTR").get_block_size_in_bytes() == 1U);
  BOOST_CHECK(
      create_encryption_context("AES-128-GCM").get_block_size_in_bytes() == 1U);
  BOOST_CHECK(
      create_encryption_context("AES-192-GCM").get_block_size_in_bytes() == 1U);
  BOOST_CHECK(
      create_encryption_context("AES-256-GCM").get_block_size_in_bytes() == 1U);
}

BOOST_FIXTURE_TEST_CASE(CipherContextGetKeySize, cipher_context_fixture) {
  BOOST_CHECK(
      create_encryption_context("AES-128-CBC").get_key_size_in_bytes() == 16U);
  BOOST_CHECK(
      create_encryption_context("AES-192-CBC").get_key_size_in_bytes() == 24U);
  BOOST_CHECK(
      create_encryption_context("AES-256-CBC").get_key_size_in_bytes() == 32U);
  BOOST_CHECK(
      create_encryption_context("AES-128-CTR").get_key_size_in_bytes() == 16U);
  BOOST_CHECK(
      create_encryption_context("AES-192-CTR").get_key_size_in_bytes() == 24U);
  BOOST_CHECK(
      create_encryption_context("AES-256-CTR").get_key_size_in_bytes() == 32U);
}

BOOST_FIXTURE_TEST_CASE(CipherContextGetIVSize, cipher_context_fixture) {
  BOOST_CHECK(create_encryption_context("AES-128-ECB").get_iv_size_in_bytes() ==
              0U);
  BOOST_CHECK(create_encryption_context("AES-192-ECB").get_iv_size_in_bytes() ==
              0U);
  BOOST_CHECK(create_encryption_context("AES-256-ECB").get_iv_size_in_bytes() ==
              0U);
  BOOST_CHECK(create_encryption_context("AES-128-CBC").get_iv_size_in_bytes() ==
              16U);
  BOOST_CHECK(create_encryption_context("AES-192-CBC").get_iv_size_in_bytes() ==
              16U);
  BOOST_CHECK(create_encryption_context("AES-256-CBC").get_iv_size_in_bytes() ==
              16U);
  BOOST_CHECK(create_encryption_context("AES-128-CTR").get_iv_size_in_bytes() ==
              16U);
  BOOST_CHECK(create_encryption_context("AES-192-CTR").get_iv_size_in_bytes() ==
              16U);
  BOOST_CHECK(create_encryption_context("AES-256-CTR").get_iv_size_in_bytes() ==
              16U);
  BOOST_CHECK(create_encryption_context("AES-128-GCM").get_iv_size_in_bytes() ==
              12U);
  BOOST_CHECK(create_encryption_context("AES-192-GCM").get_iv_size_in_bytes() ==
              12U);
  BOOST_CHECK(create_encryption_context("AES-256-GCM").get_iv_size_in_bytes() ==
              12U);
}

static const std::initializer_list<std::size_t> stream_message_sizes{
    0U, 1U, 8U, 15U, 16U, 17U, 24U, 31U, 32U, 33U};
static const std::initializer_list<std::size_t> block_message_sizes{0U, 16U,
                                                                    32U};

// Checking ECB ciphers
// (iv not required, tag not required, block_size != 1)
BOOST_DATA_TEST_CASE(CipherContextRoundtripECB,
                     boost::unit_test::data::make(bit_lengths) *
                         boost::unit_test::data::make(block_message_sizes),
                     bit_length, message_size) {
  const std::string cipher_name{"AES-" + std::to_string(bit_length) + "-ECB"};

  const std::size_t valid_key_size{
      opensslpp::cipher_context::get_key_size_in_bytes(cipher_name)};
  // ECB mode does not use an IV
  const std::size_t fake_ivec_size{16U};
  // ECB mode does not use a tag
  static constexpr std::size_t fake_tag_length{16U};

  buffer_type key{valid_key_size};
  buffer_type fake_ivec{fake_ivec_size};
  buffer_type fake_tag{fake_tag_length};
  opensslpp::crypto_rng::generate(key);

  buffer_type message{message_size};
  buffer_type encrypted_message{message_size};
  buffer_type restored_message{message_size};
  opensslpp::crypto_rng::generate(message);

  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::encryption,
                        cipher_name, key, fake_ivec, fake_tag),
                    opensslpp::core_error);
  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::encryption,
                        cipher_name, key, fake_ivec),
                    opensslpp::core_error);
  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::encryption,
                        cipher_name, key, {}, fake_tag),
                    opensslpp::core_error);
  opensslpp::cipher_context encryption_context(
      opensslpp::cipher_context_operation_type::encryption, cipher_name, key);
  BOOST_CHECK(encryption_context.get_tag_size_in_bytes() == 0U);
  BOOST_CHECK(encryption_context.get_block_size_in_bytes() != 1U);
  message.resize(message_size + 1U);
  BOOST_CHECK_THROW(encryption_context.update(message, encrypted_message),
                    opensslpp::core_error);
  message.resize(message_size);
  encrypted_message.resize(message_size + 1U);
  BOOST_CHECK_THROW(encryption_context.update(message, encrypted_message),
                    opensslpp::core_error);
  encrypted_message.resize(message_size);
  encryption_context.update(message, encrypted_message);
  BOOST_CHECK_THROW(encryption_context.finalize(fake_tag),
                    opensslpp::core_error);
  encryption_context.finalize();

  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::decryption,
                        cipher_name, key, fake_ivec, fake_tag),
                    opensslpp::core_error);
  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::decryption,
                        cipher_name, key, fake_ivec),
                    opensslpp::core_error);
  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::decryption,
                        cipher_name, key, {}, fake_tag),
                    opensslpp::core_error);
  opensslpp::cipher_context decryption_context(
      opensslpp::cipher_context_operation_type::decryption, cipher_name, key);
  BOOST_CHECK(decryption_context.get_tag_size_in_bytes() == 0U);
  BOOST_CHECK(decryption_context.get_block_size_in_bytes() != 1U);
  encrypted_message.resize(message_size + 1U);
  BOOST_CHECK_THROW(
      decryption_context.update(encrypted_message, restored_message),
      opensslpp::core_error);
  encrypted_message.resize(message_size);
  restored_message.resize(message_size + 1U);
  BOOST_CHECK_THROW(
      decryption_context.update(encrypted_message, restored_message),
      opensslpp::core_error);
  restored_message.resize(message_size);
  decryption_context.update(encrypted_message, restored_message);
  BOOST_CHECK_THROW(decryption_context.finalize(fake_tag),
                    opensslpp::core_error);
  decryption_context.finalize();

  BOOST_CHECK(message == restored_message);
}

// Checking CBC ciphers
// (iv required, tag not required, block_size != 1)
BOOST_DATA_TEST_CASE(CipherContextRoundtripCBC,
                     boost::unit_test::data::make(bit_lengths) *
                         boost::unit_test::data::make(block_message_sizes),
                     bit_length, message_size) {
  const std::string cipher_name{"AES-" + std::to_string(bit_length) + "-CBC"};

  const std::size_t valid_key_size{
      opensslpp::cipher_context::get_key_size_in_bytes(cipher_name)};
  const std::size_t valid_ivec_size{
      opensslpp::cipher_context::get_iv_size_in_bytes(cipher_name)};
  // CBC mode does not use a tag
  static constexpr std::size_t fake_tag_length{16U};

  buffer_type key{valid_key_size};
  buffer_type ivec{valid_ivec_size};
  buffer_type fake_tag{fake_tag_length};
  opensslpp::crypto_rng::generate(key);
  opensslpp::crypto_rng::generate(ivec);

  buffer_type message{message_size};
  buffer_type encrypted_message{message_size};
  buffer_type restored_message{message_size};
  opensslpp::crypto_rng::generate(message);

  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::encryption,
                        cipher_name, key, ivec, fake_tag),
                    opensslpp::core_error);
  opensslpp::cipher_context encryption_context(
      opensslpp::cipher_context_operation_type::encryption, cipher_name, key,
      ivec);
  BOOST_CHECK(encryption_context.get_tag_size_in_bytes() == 0U);
  BOOST_CHECK(encryption_context.get_block_size_in_bytes() != 1U);
  message.resize(message_size + 1U);
  BOOST_CHECK_THROW(encryption_context.update(message, encrypted_message),
                    opensslpp::core_error);
  message.resize(message_size);
  encrypted_message.resize(message_size + 1U);
  BOOST_CHECK_THROW(encryption_context.update(message, encrypted_message),
                    opensslpp::core_error);
  encrypted_message.resize(message_size);
  encryption_context.update(message, encrypted_message);
  BOOST_CHECK_THROW(encryption_context.finalize(fake_tag),
                    opensslpp::core_error);
  encryption_context.finalize();

  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::decryption,
                        cipher_name, key, ivec, fake_tag),
                    opensslpp::core_error);
  opensslpp::cipher_context decryption_context(
      opensslpp::cipher_context_operation_type::decryption, cipher_name, key,
      ivec);
  BOOST_CHECK(decryption_context.get_tag_size_in_bytes() == 0U);
  BOOST_CHECK(decryption_context.get_block_size_in_bytes() != 1U);
  encrypted_message.resize(message_size + 1U);
  BOOST_CHECK_THROW(
      decryption_context.update(encrypted_message, restored_message),
      opensslpp::core_error);
  encrypted_message.resize(message_size);
  restored_message.resize(message_size + 1U);
  BOOST_CHECK_THROW(
      decryption_context.update(encrypted_message, restored_message),
      opensslpp::core_error);
  restored_message.resize(message_size);
  decryption_context.update(encrypted_message, restored_message);
  BOOST_CHECK_THROW(decryption_context.finalize(fake_tag),
                    opensslpp::core_error);
  decryption_context.finalize();

  BOOST_CHECK(message == restored_message);
}

// Checking CTR ciphers
// (iv required, tag not required, block_size == 1)
BOOST_DATA_TEST_CASE(CipherContextRoundtripCTR,
                     boost::unit_test::data::make(bit_lengths) *
                         boost::unit_test::data::make(stream_message_sizes),
                     bit_length, message_size) {
  const std::string cipher_name{"AES-" + std::to_string(bit_length) + "-CTR"};

  const std::size_t valid_key_size{
      opensslpp::cipher_context::get_key_size_in_bytes(cipher_name)};
  const std::size_t valid_ivec_size{
      opensslpp::cipher_context::get_iv_size_in_bytes(cipher_name)};
  // CTR mode does not use a tag
  static constexpr std::size_t fake_tag_length{16U};

  buffer_type key{valid_key_size};
  buffer_type ivec{valid_ivec_size};
  buffer_type fake_tag{fake_tag_length};
  opensslpp::crypto_rng::generate(key);
  opensslpp::crypto_rng::generate(ivec);

  buffer_type message{message_size};
  buffer_type encrypted_message{message_size};
  buffer_type restored_message{message_size};
  opensslpp::crypto_rng::generate(message);

  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::encryption,
                        cipher_name, key, ivec, fake_tag),
                    opensslpp::core_error);
  opensslpp::cipher_context encryption_context(
      opensslpp::cipher_context_operation_type::encryption, cipher_name, key,
      ivec);
  BOOST_CHECK(encryption_context.get_tag_size_in_bytes() == 0U);
  BOOST_CHECK(encryption_context.get_block_size_in_bytes() == 1U);
  encrypted_message.resize(message_size + 1U);
  BOOST_CHECK_THROW(encryption_context.update(message, encrypted_message),
                    opensslpp::core_error);
  encrypted_message.resize(message_size);
  encryption_context.update(message, encrypted_message);
  BOOST_CHECK_THROW(encryption_context.finalize(fake_tag),
                    opensslpp::core_error);
  encryption_context.finalize();

  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::decryption,
                        cipher_name, key, ivec, fake_tag),
                    opensslpp::core_error);
  opensslpp::cipher_context decryption_context(
      opensslpp::cipher_context_operation_type::decryption, cipher_name, key,
      ivec);
  BOOST_CHECK(decryption_context.get_tag_size_in_bytes() == 0U);
  BOOST_CHECK(decryption_context.get_block_size_in_bytes() == 1U);
  restored_message.resize(message_size + 1U);
  BOOST_CHECK_THROW(
      decryption_context.update(encrypted_message, restored_message),
      opensslpp::core_error);
  restored_message.resize(message_size);
  decryption_context.update(encrypted_message, restored_message);
  BOOST_CHECK_THROW(decryption_context.finalize(fake_tag),
                    opensslpp::core_error);
  decryption_context.finalize();

  BOOST_CHECK(message == restored_message);
}

// Checking GCM ciphers
// (iv required, tag required, block_size == 1)
BOOST_DATA_TEST_CASE(CipherContextRoundtripGCM,
                     boost::unit_test::data::make(bit_lengths) *
                         boost::unit_test::data::make(stream_message_sizes),
                     bit_length, message_size) {
  const std::string cipher_name{"AES-" + std::to_string(bit_length) + "-GCM"};

  const std::size_t valid_key_size{
      opensslpp::cipher_context::get_key_size_in_bytes(cipher_name)};
  const std::size_t valid_ivec_size{
      opensslpp::cipher_context::get_iv_size_in_bytes(cipher_name)};
  buffer_type key{valid_key_size};
  buffer_type ivec{valid_ivec_size};
  buffer_type tag{};
  opensslpp::crypto_rng::generate(key);
  opensslpp::crypto_rng::generate(ivec);

  buffer_type message{message_size};
  buffer_type encrypted_message{message_size};
  buffer_type restored_message{message_size};
  opensslpp::crypto_rng::generate(message);

  tag.resize(1U);
  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::encryption,
                        cipher_name, key, ivec, tag),
                    opensslpp::core_error);
  opensslpp::cipher_context encryption_context(
      opensslpp::cipher_context_operation_type::encryption, cipher_name, key,
      ivec);
  const std::size_t tag_length{encryption_context.get_tag_size_in_bytes()};
  BOOST_CHECK(tag_length != 0U);
  BOOST_CHECK(encryption_context.get_block_size_in_bytes() == 1U);
  encrypted_message.resize(message_size + 1U);
  BOOST_CHECK_THROW(encryption_context.update(message, encrypted_message),
                    opensslpp::core_error);
  encrypted_message.resize(message_size);
  encryption_context.update(message, encrypted_message);
  BOOST_CHECK_THROW(encryption_context.finalize(), opensslpp::core_error);
  tag.resize(tag_length + 1U);
  BOOST_CHECK_THROW(encryption_context.finalize(tag), opensslpp::core_error);
  tag.resize(tag_length);
  encryption_context.finalize(tag);

  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::decryption,
                        cipher_name, key, ivec),
                    opensslpp::core_error);
  tag.resize(tag_length + 1U);
  BOOST_CHECK_THROW(opensslpp::cipher_context(
                        opensslpp::cipher_context_operation_type::decryption,
                        cipher_name, key, ivec, tag),
                    opensslpp::core_error);
  tag.resize(tag_length);
  opensslpp::cipher_context decryption_context(
      opensslpp::cipher_context_operation_type::decryption, cipher_name, key,
      ivec, tag);
  BOOST_CHECK(decryption_context.get_tag_size_in_bytes() == tag_length);
  BOOST_CHECK(decryption_context.get_block_size_in_bytes() == 1U);
  restored_message.resize(message_size + 1U);
  BOOST_CHECK_THROW(
      decryption_context.update(encrypted_message, restored_message),
      opensslpp::core_error);
  restored_message.resize(message_size);
  decryption_context.update(encrypted_message, restored_message);
  BOOST_CHECK_THROW(decryption_context.finalize(tag), opensslpp::core_error);
  decryption_context.finalize();

  BOOST_CHECK(message == restored_message);
}

BOOST_DATA_TEST_CASE(CipherContextUpdatedIVCTR,
                     boost::unit_test::data::make(bit_lengths) *
                         boost::unit_test::data::make(stream_message_sizes),
                     bit_length, message_size) {
  const std::string cipher_name{"AES-" + std::to_string(bit_length) + "-CTR"};

  const std::size_t valid_key_size{
      opensslpp::cipher_context::get_key_size_in_bytes(cipher_name)};
  const std::size_t valid_ivec_size{
      opensslpp::cipher_context::get_iv_size_in_bytes(cipher_name)};

  buffer_type key{valid_key_size};
  buffer_type ivec{valid_ivec_size};
  opensslpp::crypto_rng::generate(key);
  opensslpp::crypto_rng::generate(ivec);

  buffer_type message{message_size};
  buffer_type encrypted_message{message_size};
  opensslpp::crypto_rng::generate(message);

  opensslpp::cipher_context encryption_context(
      opensslpp::cipher_context_operation_type::encryption, cipher_name, key,
      ivec);
  encryption_context.update(message, encrypted_message);

  buffer_type updated_ivec{valid_ivec_size};
  encryption_context.extract_updated_iv(updated_ivec);

  auto fast_encryption_context{opensslpp::cipher_context::create_with_offset(
      std::size(message), opensslpp::cipher_context_operation_type::encryption,
      cipher_name, key, ivec)};
  buffer_type fast_updated_ivec{valid_ivec_size};
  fast_encryption_context.extract_updated_iv(fast_updated_ivec);

  BOOST_CHECK(updated_ivec == fast_updated_ivec);
}

BOOST_DATA_TEST_CASE(CipherContextCTRResume,
                     boost::unit_test::data::make(bit_lengths) *
                         boost::unit_test::data::make(stream_message_sizes),
                     bit_length, message_size) {
  const std::string cipher_name{"AES-" + std::to_string(bit_length) + "-CTR"};

  const std::size_t valid_key_size{
      opensslpp::cipher_context::get_key_size_in_bytes(cipher_name)};
  const std::size_t valid_ivec_size{
      opensslpp::cipher_context::get_iv_size_in_bytes(cipher_name)};

  buffer_type key{valid_key_size};
  buffer_type ivec{valid_ivec_size};
  opensslpp::crypto_rng::generate(key);
  opensslpp::crypto_rng::generate(ivec);

  const std::size_t first_part_size{message_size / 2U};
  const std::size_t second_part_size{message_size - first_part_size};

  buffer_type message{message_size};
  opensslpp::crypto_rng::generate(message);

  const util::const_byte_span message_v{message};
  const util::const_byte_span message_first_part_v{
      message_v.first(first_part_size)};
  const util::const_byte_span message_second_part_v{
      message_v.last(second_part_size)};

  // single pass encryption
  buffer_type encrypted_message_single_pass{message_size};
  {
    opensslpp::cipher_context encryption_context(
        opensslpp::cipher_context_operation_type::encryption, cipher_name, key,
        ivec);
    encryption_context.update(message_v, encrypted_message_single_pass);
    encryption_context.finalize();
  }

  // encrypting with 2 update calls
  buffer_type encrypted_message_partial_updates{message_size};
  {
    const util::byte_span encrypted_message_partial_updates_v{
        encrypted_message_partial_updates};
    const util::byte_span encrypted_message_partial_updates_first_part_v{
        encrypted_message_partial_updates_v.first(first_part_size)};
    const util::byte_span encrypted_message_partial_updates_second_part_v{
        encrypted_message_partial_updates_v.last(second_part_size)};

    opensslpp::cipher_context encryption_context(
        opensslpp::cipher_context_operation_type::encryption, cipher_name, key,
        ivec);
    encryption_context.update(message_first_part_v,
                              encrypted_message_partial_updates_first_part_v);
    encryption_context.update(message_second_part_v,
                              encrypted_message_partial_updates_second_part_v);
    encryption_context.finalize();
  }

  BOOST_CHECK(encrypted_message_partial_updates ==
              encrypted_message_single_pass);

  // encrypting with context re-creation (resume)
  buffer_type encrypted_message_resume{message_size};
  {
    const util::byte_span encrypted_message_resume_v{encrypted_message_resume};
    const util::byte_span encrypted_message_resume_first_part_v{
        encrypted_message_resume_v.first(first_part_size)};
    const util::byte_span encrypted_message_resume_second_part_v{
        encrypted_message_resume_v.last(second_part_size)};

    opensslpp::cipher_context initial_encryption_context(
        opensslpp::cipher_context_operation_type::encryption, cipher_name, key,
        ivec);
    initial_encryption_context.update(message_first_part_v,
                                      encrypted_message_resume_first_part_v);
    initial_encryption_context.finalize();

    auto resumed_encryption_context{
        opensslpp::cipher_context::create_with_offset(
            first_part_size,
            opensslpp::cipher_context_operation_type::encryption, cipher_name,
            key, ivec)};
    resumed_encryption_context.update(message_second_part_v,
                                      encrypted_message_resume_second_part_v);
    resumed_encryption_context.finalize();
  }

  BOOST_CHECK(encrypted_message_resume == encrypted_message_single_pass);
}
