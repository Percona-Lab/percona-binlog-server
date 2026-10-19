// Copyright (c) 2026 Percona and/or its affiliates.
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
#include <string_view>
#include <vector>

#define BOOST_TEST_MODULE HeartbeatLogV2EventTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/unit_test_suite.hpp>

#include <boost/test/tools/interface.hpp>

#include <boost/test/tools/old/interface.hpp>

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_header.hpp"
#include "binsrv/events/composite_binlog_name.hpp"
#include "binsrv/events/event.hpp"
#include "binsrv/events/event_view.hpp"
#include "binsrv/events/heartbeat_log_v2_body_impl.hpp"
#include "binsrv/events/reader_context.hpp"

#include "binsrv/replication_mode_type.hpp"

#include "util/byte_span.hpp"
#include "util/ctime_timestamp.hpp"
#include "util/semantic_version.hpp"

namespace {

using heartbeat_body = binsrv::events::generic_body_impl<
    binsrv::events::code_type::heartbeat_log_v2>;

[[nodiscard]] heartbeat_body make_body(std::string_view binlog_name,
                                       std::uint64_t position) {
  return heartbeat_body{
      binsrv::events::composite_binlog_name::parse(binlog_name), position};
}

[[nodiscard]] std::vector<std::byte> encode(const heartbeat_body &body) {
  std::vector<std::byte> buffer(body.calculate_encoded_size());
  util::byte_span destination{buffer};
  body.encode_to(destination);
  // encode_to advances 'destination' past everything it wrote
  BOOST_TEST(destination.empty());
  return buffer;
}

} // namespace

// The body is a TLV stream of packed integers:
//   [type=1][len][filename bytes][type=2][len][packed position][type=0]
// For "binlog.000001" at position 4 the exact bytes are known, so assert them
// to pin the wire format down.
BOOST_AUTO_TEST_CASE(EncodesExpectedTlvBytes) {
  const auto buffer{encode(make_body("binlog.000001", 4))};

  const std::vector<std::byte> expected{
      std::byte{0x01}, std::byte{0x0D}, std::byte{'b'}, std::byte{'i'},
      std::byte{'n'},  std::byte{'l'},  std::byte{'o'}, std::byte{'g'},
      std::byte{'.'},  std::byte{'0'},  std::byte{'0'}, std::byte{'0'},
      std::byte{'0'},  std::byte{'0'},  std::byte{'1'}, std::byte{0x02},
      std::byte{0x01}, std::byte{0x04}, std::byte{0x00}};

  BOOST_TEST(buffer == expected);
  BOOST_CHECK_EQUAL(make_body("binlog.000001", 4).calculate_encoded_size(),
                    expected.size());
}

// A position that does not fit in a single byte is stored as a multi-byte
// packed integer, and the field's length reflects that width.
BOOST_AUTO_TEST_CASE(EncodesMultiBytePosition) {
  const auto buffer{encode(make_body("binlog.000001", 300))};

  // ...[type=2][len=3][0xFC 0x2C 0x01][type=0]
  const std::vector<std::byte> position_tlv{std::byte{0x02}, std::byte{0x03},
                                            std::byte{0xFC}, std::byte{0x2C},
                                            std::byte{0x01}, std::byte{0x00}};
  const std::vector<std::byte> tail(buffer.end() - 6, buffer.end());
  BOOST_TEST(tail == position_tlv);
}

// Encoding and then decoding yields an equal object and recovers both fields.
BOOST_AUTO_TEST_CASE(RoundTripSmallPosition) {
  const auto original{make_body("binlog.000042", 4)};
  const auto buffer{encode(original)};

  const heartbeat_body parsed{util::const_byte_span{buffer}};
  BOOST_TEST((parsed == original));
  BOOST_CHECK_EQUAL(parsed.get_readable_binlog(), "binlog.000042");
  BOOST_CHECK_EQUAL(parsed.get_position(), 4U);
}

BOOST_AUTO_TEST_CASE(RoundTripLargePosition) {
  // a position larger than 4 GB exercises the 8-byte packed integer path and
  // confirms the 64-bit position survives the round trip (the whole point of
  // the v2 event over v1, which truncates the position to 32 bits)
  constexpr std::uint64_t large_position{0x1'0000'0001ULL};
  const auto original{make_body("binlog.123456", large_position)};
  const auto buffer{encode(original)};

  const heartbeat_body parsed{util::const_byte_span{buffer}};
  BOOST_TEST((parsed == original));
  BOOST_CHECK_EQUAL(parsed.get_position(), large_position);
}

// Assemble a whole event (common header + empty post header + body + checksum)
// through the generic event machinery and parse it back, confirming the event
// is registered for code 41 and survives a full round trip with a checksum.
BOOST_AUTO_TEST_CASE(FullEventRoundTripWithChecksum) {
  constexpr auto code{binsrv::events::code_type::heartbeat_log_v2};
  const util::semantic_version server_version{"8.4.8"};
  constexpr std::uint32_t offset{1000U};
  constexpr std::uint32_t server_id{12345U};

  const binsrv::events::reader_context context{
      server_version.get_encoded(), true /* verify checksum */,
      binsrv::replication_mode_type::gtid, "", offset};

  const auto body{make_body("binlog.000007", 4U)};
  binsrv::events::event_storage buffer;
  const auto generated{binsrv::events::event::create_event<code>(
      offset, util::ctime_timestamp{}, server_id,
      binsrv::events::common_header_flag_set{},
      binsrv::events::generic_post_header<code>{}, body, true /* checksum */,
      buffer)};

  BOOST_CHECK_EQUAL(
      static_cast<int>(generated.get_common_header().get_type_code_raw()),
      static_cast<int>(code));

  const binsrv::events::event_view generated_view{
      context, util::const_byte_span{buffer}};
  const binsrv::events::event parsed{generated_view};

  BOOST_CHECK_EQUAL(generated, parsed);
  BOOST_CHECK((parsed.get_body<code>() == body));
  BOOST_CHECK_EQUAL(parsed.get_body<code>().get_position(), 4U);
  BOOST_CHECK_EQUAL(parsed.get_body<code>().get_readable_binlog(),
                    "binlog.000007");
}
