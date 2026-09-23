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
#include <iomanip>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>

#define BOOST_TEST_MODULE DigestContextTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/unit_test_suite.hpp>

#include <boost/test/tools/old/interface.hpp>

#include "opensslpp/core_error.hpp"
#include "opensslpp/digest_context.hpp"

namespace {

// FIPS 180-4 short-message sample: SHA-256("abc")
constexpr std::string_view sha256_abc_hex{
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};

// FIPS 180-4 long-message sample input and its SHA-256 digest:
constexpr std::string_view long_input{
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
constexpr std::string_view sha256_long_hex{
    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"};

std::string to_hex(std::string_view raw) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (const auto raw_char : raw) {
    oss << std::setw(2)
        << static_cast<unsigned int>(static_cast<unsigned char>(raw_char));
  }
  return oss.str();
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE(DigestContextDefaultIsEmpty) {
  const opensslpp::digest_context ctx{};
  BOOST_CHECK(ctx.is_empty());
}

BOOST_AUTO_TEST_CASE(DigestContextCalculateKnownVector) {
  const auto digest{
      opensslpp::digest_context::calculate(std::string{"SHA256"}, "abc")};
  BOOST_CHECK_EQUAL(to_hex(digest), std::string{sha256_abc_hex});
}

BOOST_AUTO_TEST_CASE(DigestContextStreamedMatchesOneShot) {
  constexpr std::size_t first_chunk_size{10U};
  constexpr std::size_t second_chunk_size{20U};

  opensslpp::digest_context ctx{std::string{"SHA256"}};
  ctx.update(long_input.substr(0U, first_chunk_size));
  ctx.update(long_input.substr(first_chunk_size, second_chunk_size));
  ctx.update(long_input.substr(first_chunk_size + second_chunk_size));
  const auto streamed{ctx.finalize()};
  BOOST_CHECK_EQUAL(to_hex(streamed), std::string{sha256_long_hex});

  const auto one_shot{
      opensslpp::digest_context::calculate(std::string{"SHA256"}, long_input)};
  BOOST_CHECK(streamed == one_shot);
}

BOOST_AUTO_TEST_CASE(DigestContextFinalizeConsumesContext) {
  opensslpp::digest_context ctx{std::string{"SHA256"}};
  static_cast<void>(ctx.finalize());
  BOOST_CHECK(ctx.is_empty());
}

BOOST_AUTO_TEST_CASE(DigestContextInvalidNameThrows) {
  BOOST_CHECK_THROW(opensslpp::digest_context{std::string{"INVALID-DIGEST"}},
                    opensslpp::core_error);
}
