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

#include <cstdint>
#include <string_view>

#define BOOST_TEST_MODULE EventTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/unit_test_suite.hpp>

#include <boost/test/tools/old/interface.hpp>

#include "binsrv/ctime_timestamp.hpp"
#include "binsrv/replication_mode_type.hpp"

#include "binsrv/gtids/gtid_set.hpp"

#include "binsrv/events/checksum_algorithm_type.hpp"
#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_header.hpp"
#include "binsrv/events/common_header_flag_type.hpp"
#include "binsrv/events/event.hpp"
#include "binsrv/events/protocol_traits_fwd.hpp"
#include "binsrv/events/reader_context.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/semantic_version.hpp"

static constexpr std::uint32_t default_server_id{42U};

BOOST_AUTO_TEST_CASE(EventRoundTrip) {
  const util::semantic_version server_version{"8.4.8"};
  std::uint32_t offset{0U};
  binsrv::events::reader_context context{server_version.get_encoded(), true,
                                         binsrv::replication_mode_type::gtid,
                                         "", offset};

  binsrv::events::event_storage event_buffer;

  // artificial ROTATE event
  offset = 0U;
  const binsrv::events::common_header_flag_set flags{
      binsrv::events::common_header_flag_type::artificial};

  const binsrv::events::generic_post_header<binsrv::events::code_type::rotate>
      rotate_post_header{binsrv::events::magic_binlog_offset};
  const binsrv::events::generic_body<binsrv::events::code_type::rotate>
      rotate_body{"binlog.000001"};

  const auto generated_rotate_event{
      binsrv::events::event::create_event<binsrv::events::code_type::rotate>(
          offset,
          // artificial ROTATE event must include zero timestamp
          binsrv::ctime_timestamp{}, default_server_id, flags,
          rotate_post_header, rotate_body, true, event_buffer)};

  const binsrv::events::event parsed_rotate_event{
      context, util::const_byte_span{event_buffer}};

  BOOST_CHECK_EQUAL(generated_rotate_event, parsed_rotate_event);

  // FORMAT_DESCRIPTION event
  offset = binsrv::events::magic_binlog_offset;
  const binsrv::events::generic_post_header<
      binsrv::events::code_type::format_description>
      format_description_post_header{
          binsrv::events::default_binlog_version, server_version,
          binsrv::ctime_timestamp::now(),
          binsrv::events::default_common_header_length,
          binsrv::events::reader_context::get_hardcoded_post_header_lengths(
              server_version.get_encoded())};
  const binsrv::events::generic_body<
      binsrv::events::code_type::format_description>
      format_description_body{binsrv::events::checksum_algorithm_type::crc32};

  const auto generated_format_description_event{
      binsrv::events::event::create_event<
          binsrv::events::code_type::format_description>(
          offset, binsrv::ctime_timestamp::now(), default_server_id,
          binsrv::events::common_header_flag_set{},
          format_description_post_header, format_description_body, true,
          event_buffer)};

  const binsrv::events::event parsed_format_description_event{
      context, util::const_byte_span{event_buffer}};

  BOOST_CHECK_EQUAL(generated_format_description_event,
                    parsed_format_description_event);

  // PREVIOUS_GTIDS_LOG event
  offset = generated_format_description_event.get_common_header()
               .get_next_event_position_raw();
  const binsrv::events::generic_post_header<
      binsrv::events::code_type::previous_gtids_log>
      previous_gtids_log_post_header{};
  const binsrv::events::generic_body<
      binsrv::events::code_type::previous_gtids_log>
      previous_gtids_log_body{binsrv::gtids::gtid_set{
          "11111111-aaaa-1111-aaaa-111111111111:1:3-5"}};
  const auto generated_previous_gtids_log_event{
      binsrv::events::event::create_event<
          binsrv::events::code_type::previous_gtids_log>(
          offset, binsrv::ctime_timestamp::now(), default_server_id,
          binsrv::events::common_header_flag_set{},
          previous_gtids_log_post_header, previous_gtids_log_body, true,
          event_buffer)};

  const binsrv::events::event parsed_previous_gtids_log_event{
      context, util::const_byte_span{event_buffer}};

  BOOST_CHECK_EQUAL(generated_previous_gtids_log_event,
                    parsed_previous_gtids_log_event);
}
