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
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>

#define BOOST_TEST_MODULE SslAcceptorContextTests
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test_suite.hpp>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/types.h>
#include <openssl/x509.h>

#include "minimysql/ssl_acceptor_context.hpp"

namespace {

void write_temp_file(const std::string &path, std::string_view content) {
  std::ofstream stream{path, std::ios::binary | std::ios::trunc};
  BOOST_REQUIRE(stream.is_open());
  stream.write(std::data(content),
               static_cast<std::streamsize>(std::size(content)));
  BOOST_REQUIRE(stream.good());
}

// Generate a fresh 2048-bit RSA key and return an owning EVP_PKEY handle.
EVP_PKEY *generate_rsa_keypair() {
  EVP_PKEY_CTX *ctx{EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr)};
  BOOST_REQUIRE(ctx != nullptr);
  BOOST_REQUIRE(EVP_PKEY_keygen_init(ctx) > 0);
  BOOST_REQUIRE(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) > 0);
  EVP_PKEY *key{nullptr};
  BOOST_REQUIRE(EVP_PKEY_keygen(ctx, &key) > 0);
  EVP_PKEY_CTX_free(ctx);
  return key;
}

// Build a minimal self-signed X.509 certificate signed by `key`.
X509 *build_self_signed_certificate(EVP_PKEY *key, const char *common_name) {
  constexpr long seconds_per_minute{60L};
  constexpr long minutes_per_hour{60L};
  constexpr long hours_per_day{24L};
  constexpr long validity_days{30L};
  constexpr long validity_seconds{seconds_per_minute * minutes_per_hour *
                                  hours_per_day * validity_days};

  X509 *cert{X509_new()};
  BOOST_REQUIRE(cert != nullptr);
  BOOST_REQUIRE(X509_set_version(cert, 2) == 1); // X509v3
  BOOST_REQUIRE(ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) == 1);
  X509_gmtime_adj(X509_get_notBefore(cert), 0);
  X509_gmtime_adj(X509_get_notAfter(cert), validity_seconds);
  BOOST_REQUIRE(X509_set_pubkey(cert, key) == 1);

  X509_NAME *name{X509_get_subject_name(cert)};
  BOOST_REQUIRE(name != nullptr);
  BOOST_REQUIRE(
      X509_NAME_add_entry_by_txt(
          name, "CN", MBSTRING_ASC,
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<const unsigned char *>(common_name), -1, -1,
          0) == 1);
  BOOST_REQUIRE(X509_set_issuer_name(cert, name) == 1);

  BOOST_REQUIRE(X509_sign(cert, key, EVP_sha256()) > 0);
  return cert;
}

std::string pem_encode_private_key(EVP_PKEY *key) {
  BIO *bio{BIO_new(BIO_s_mem())};
  BOOST_REQUIRE(bio != nullptr);
  BOOST_REQUIRE(PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr,
                                         nullptr) == 1);
  char *data{nullptr};
  const long length{BIO_get_mem_data(bio, &data)};
  BOOST_REQUIRE(length > 0);
  std::string result{data, static_cast<std::size_t>(length)};
  BIO_free(bio);
  return result;
}

std::string pem_encode_certificate(X509 *cert) {
  BIO *bio{BIO_new(BIO_s_mem())};
  BOOST_REQUIRE(bio != nullptr);
  BOOST_REQUIRE(PEM_write_bio_X509(bio, cert) == 1);
  char *data{nullptr};
  const long length{BIO_get_mem_data(bio, &data)};
  BOOST_REQUIRE(length > 0);
  std::string result{data, static_cast<std::size_t>(length)};
  BIO_free(bio);
  return result;
}

struct pem_pair {
  std::string cert_path;
  std::string key_path;
};

pem_pair make_valid_pem_pair(const std::string &path_prefix) {
  EVP_PKEY *key{generate_rsa_keypair()};
  X509 *cert{build_self_signed_certificate(key, "minimysql-test")};

  const std::string cert_path{path_prefix + "_cert.pem"};
  const std::string key_path{path_prefix + "_key.pem"};

  write_temp_file(cert_path, pem_encode_certificate(cert));
  write_temp_file(key_path, pem_encode_private_key(key));

  X509_free(cert);
  EVP_PKEY_free(key);

  return {.cert_path = cert_path, .key_path = key_path};
}

} // namespace

BOOST_AUTO_TEST_SUITE(ssl_acceptor_context_tests)

BOOST_AUTO_TEST_CASE(constructs_with_valid_cert_and_key) {
  const auto files{make_valid_pem_pair("/tmp/minimysql_ssl_valid")};

  BOOST_CHECK_NO_THROW(
      minimysql::ssl_acceptor_context(files.cert_path, files.key_path));

  minimysql::ssl_acceptor_context ssl_ctx{files.cert_path, files.key_path};
  BOOST_CHECK(ssl_ctx.native().native_handle() != nullptr);
  BOOST_CHECK_EQUAL(ssl_ctx.get_certificate_path(), files.cert_path);
  BOOST_CHECK_EQUAL(ssl_ctx.get_private_key_path(), files.key_path);
}

BOOST_AUTO_TEST_CASE(throws_on_missing_cert_file) {
  const auto files{make_valid_pem_pair("/tmp/minimysql_ssl_missing_cert")};

  const std::string bogus_cert{"/tmp/minimysql_ssl_does_not_exist.pem"};
  BOOST_CHECK_EXCEPTION(
      minimysql::ssl_acceptor_context(bogus_cert, files.key_path),
      std::runtime_error, [&bogus_cert](const std::runtime_error &exc) {
        return std::string{exc.what()}.find(bogus_cert) != std::string::npos;
      });
}

BOOST_AUTO_TEST_CASE(throws_on_mismatched_key) {
  const auto files_a{make_valid_pem_pair("/tmp/minimysql_ssl_pair_a")};
  const auto files_b{make_valid_pem_pair("/tmp/minimysql_ssl_pair_b")};

  try {
    // cert from pair A + key from pair B — private key does not match cert.
    const minimysql::ssl_acceptor_context ssl_ctx{files_a.cert_path,
                                                  files_b.key_path};
    BOOST_FAIL("expected exception was not thrown");
  } catch (const std::runtime_error &exc) {
    const std::string what{exc.what()};
    // Depending on the OpenSSL version, the mismatch may surface either from
    // SSL_CTX_use_PrivateKey_file (which internally checks the key against
    // any already-loaded cert on modern OpenSSL) or from our explicit
    // SSL_CTX_check_private_key call. Either failure references the key path
    // and reports a key-values/cert mismatch — assert on both.
    BOOST_CHECK(what.find(files_b.key_path) != std::string::npos);
    const bool mentions_mismatch =
        what.find("does not match") != std::string::npos ||
        what.find("key values mismatch") != std::string::npos ||
        what.find("KEY_VALUES_MISMATCH") != std::string::npos;
    BOOST_CHECK_MESSAGE(mentions_mismatch,
                        "unexpected mismatch message: " + what);
  }
}

BOOST_AUTO_TEST_SUITE_END()
