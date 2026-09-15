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
#include <string>
#include <string_view>

#define BOOST_TEST_MODULE ConnectionContextSslTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test_suite.hpp>

#include <boost/asio/buffer.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"

#include "mysqlrouter/classic_protocol_codec_base.h"
#include "mysqlrouter/classic_protocol_codec_frame.h"   // IWYU pragma: keep
#include "mysqlrouter/classic_protocol_codec_message.h" // IWYU pragma: keep
#include "mysqlrouter/classic_protocol_constants.h"
#include "mysqlrouter/classic_protocol_frame.h"
#include "mysqlrouter/classic_protocol_message.h"

#pragma GCC diagnostic pop

#include "minimysql/connection_context.hpp"
#include "minimysql/network_io_operations_fwd.hpp"

namespace {

constexpr std::string_view test_username{"rpl"};
constexpr std::string_view test_password{"password"};

// Bit position of CLIENT_SSL in the MySQL capability flags word (24-bit region
// visible in the server Greeting; the low 16 bits are followed by 3 fixed
// bytes and the high 8 bits). We test at the classic_protocol level rather
// than by counting bytes, so we do not need to know the exact byte offset.
constexpr std::size_t client_ssl_bit{classic_protocol::capabilities::pos::ssl};

// Encode a fabricated Protocol::SSLRequest frame using the classic_protocol
// codec. This is the same shape the mysql CLI sends when
// --ssl-mode>=PREFERRED and the server advertised CLIENT_SSL: the greeting
// carries the ssl capability bit but everything from username onward is
// empty, and the codec truncates the packet accordingly.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
minimysql::network_buffer_type
encode_sslrequest_frame(classic_protocol::capabilities::value_type server_caps,
                        std::uint8_t sequence_number,
                        std::uint32_t max_packet_size = 16UL * 1024UL * 1024UL,
                        std::uint8_t collation = 255U) {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  const classic_protocol::capabilities::value_type client_caps =
      server_caps | classic_protocol::capabilities::ssl;

  const classic_protocol::message::client::Greeting sslrequest{
      client_caps,
      max_packet_size,
      collation,
      // username / auth-method-data / schema / auth-method-name / attributes
      // all empty — this is the SSLRequest shape.
      {},
      {},
      {},
      {},
      {}};

  using ssl_request_frame = classic_protocol::frame::Frame<
      classic_protocol::message::client::Greeting>;

  minimysql::network_buffer_type buffer{};
  auto encode_result = classic_protocol::encode<ssl_request_frame>(
      {sequence_number, sslrequest}, server_caps,
      boost::asio::dynamic_buffer(buffer));
  if (!encode_result) {
    throw std::runtime_error{"encoding SSLRequest failed"};
  }
  return buffer;
}

} // namespace

BOOST_AUTO_TEST_SUITE(connection_context_ssl_tests)

BOOST_AUTO_TEST_CASE(mark_transport_secure_flips_connection_is_secure) {
  minimysql::connection_context context{test_username, test_password};
  BOOST_CHECK(!context.connection_is_secure());
  context.mark_transport_secure();
  BOOST_CHECK(context.connection_is_secure());
}

BOOST_AUTO_TEST_CASE(default_greeting_does_not_advertise_ssl) {
  minimysql::connection_context context{test_username, test_password};
  [[maybe_unused]] const auto greeting =
      context.generate_encoded_server_greeting();
  BOOST_CHECK(!context.get_server_capabilities().test(client_ssl_bit));
}

BOOST_AUTO_TEST_CASE(
    enabling_ssl_capability_only_changes_ssl_bit_in_server_capabilities) {
  minimysql::connection_context baseline{test_username, test_password};
  [[maybe_unused]] const auto baseline_greeting =
      baseline.generate_encoded_server_greeting();
  const auto baseline_caps = baseline.get_server_capabilities();

  minimysql::connection_context ssl_enabled{
      test_username,
      test_password,
      {},
      {},
      /* ssl_capability_enabled = */ true};
  [[maybe_unused]] const auto ssl_greeting =
      ssl_enabled.generate_encoded_server_greeting();
  const auto ssl_caps = ssl_enabled.get_server_capabilities();

  BOOST_CHECK(!baseline_caps.test(client_ssl_bit));
  BOOST_CHECK(ssl_caps.test(client_ssl_bit));

  // Only the SSL bit differs.
  const auto xor_bits = baseline_caps ^ ssl_caps;
  BOOST_CHECK_EQUAL(xor_bits.count(), 1U);
  BOOST_CHECK(xor_bits.test(client_ssl_bit));
}

BOOST_AUTO_TEST_CASE(sslrequest_recognised_as_short_greeting) {
  minimysql::connection_context context{test_username,
                                        test_password,
                                        {},
                                        {},
                                        /* ssl_capability_enabled = */ true};

  // The server must have generated the greeting first so that server_caps and
  // sequence-number progression are initialised the same way as in a real
  // session.
  [[maybe_unused]] const auto server_greeting =
      context.generate_encoded_server_greeting();

  // Sequence number of the SSLRequest is 1 (server used 0 for its greeting).
  const auto sslrequest_frame =
      encode_sslrequest_frame(context.get_server_capabilities(), 1U);

  context.parse_client_greeting(sslrequest_frame);

  BOOST_CHECK(context.is_sslrequest_greeting());
  BOOST_CHECK(context.get_client_username().empty());
  BOOST_CHECK(context.get_shared_capabilities().test(client_ssl_bit));
}

BOOST_AUTO_TEST_CASE(non_ssl_greeting_is_not_flagged_as_sslrequest) {
  minimysql::connection_context context{test_username,
                                        test_password,
                                        {},
                                        {},
                                        /* ssl_capability_enabled = */ true};

  [[maybe_unused]] const auto server_greeting =
      context.generate_encoded_server_greeting();

  // Encode a normal (non-SSL) client greeting with a real username. The
  // client did not set CLIENT_SSL.
  const classic_protocol::capabilities::value_type client_caps =
      context.get_server_capabilities() & ~classic_protocol::capabilities::ssl;

  const classic_protocol::message::client::Greeting normal_greeting{
      client_caps,
      16UL * 1024UL * 1024UL,
      255U,
      std::string{"rpl"},
      {},
      {},
      std::string{"caching_sha2_password"},
      {}};

  using client_greeting_frame = classic_protocol::frame::Frame<
      classic_protocol::message::client::Greeting>;

  minimysql::network_buffer_type buffer{};
  const auto encode_result = classic_protocol::encode<client_greeting_frame>(
      {1U, normal_greeting}, context.get_server_capabilities(),
      boost::asio::dynamic_buffer(buffer));
  BOOST_REQUIRE(encode_result);

  context.parse_client_greeting(buffer);

  BOOST_CHECK(!context.is_sslrequest_greeting());
  BOOST_CHECK_EQUAL(context.get_client_username(), "rpl");
}

BOOST_AUTO_TEST_SUITE_END()
