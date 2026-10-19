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

#include <regex>
#include <string>
#include <string_view>

#define BOOST_TEST_MODULE HeartbeatQueryPatternTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/unit_test_suite.hpp>

#include <boost/test/tools/old/interface.hpp>

#include "minimysql/handshake_query_patterns.hpp"

namespace {

// Builds the heartbeat-period matcher exactly as the handshake command loop
// does, so this test exercises the regular expression that is used in
// production.
[[nodiscard]] std::regex make_heartbeat_period_regex() {
  return std::regex{std::string{minimysql::heartbeat_period_query_pattern},
                    std::regex::icase};
}

// Returns the captured nanosecond count, or an empty string when the statement
// is not recognized as a heartbeat-period request.
[[nodiscard]] std::string match_heartbeat_period(const std::string &statement) {
  const std::regex heartbeat_period_regex{make_heartbeat_period_regex()};
  std::smatch match;
  if (!std::regex_search(statement, match, heartbeat_period_regex)) {
    return {};
  }
  return match[1].str();
}

// Reports whether 'statement' is recognized by 'pattern', compiled exactly as
// the handshake command loop compiles every handshake pattern.
[[nodiscard]] bool matches(std::string_view pattern,
                           const std::string &statement) {
  const std::regex query_regex{std::string{pattern}, std::regex::icase};
  std::smatch match;
  return std::regex_search(statement, match, query_regex);
}

} // namespace

// The statement a replica actually sends assigns the same nanosecond value to
// both the legacy and the modern user variable in a single statement, with the
// legacy name first. We must capture the period from it.
BOOST_AUTO_TEST_CASE(RealCombinedStatementMasterFirst) {
  BOOST_CHECK_EQUAL(
      match_heartbeat_period("SET @master_heartbeat_period = 30000000000, "
                             "@source_heartbeat_period = 30000000000"),
      "30000000000");
}

// Forward-compatible with a replica that leads with the modern variable.
BOOST_AUTO_TEST_CASE(CombinedStatementSourceFirst) {
  BOOST_CHECK_EQUAL(
      match_heartbeat_period("SET @source_heartbeat_period = 500, "
                             "@master_heartbeat_period = 500"),
      "500");
}

// A lone assignment of either variable is accepted as well.
BOOST_AUTO_TEST_CASE(LoneMasterAssignment) {
  BOOST_CHECK_EQUAL(
      match_heartbeat_period("SET @master_heartbeat_period = 1000000000"),
      "1000000000");
}

BOOST_AUTO_TEST_CASE(LoneSourceAssignment) {
  BOOST_CHECK_EQUAL(
      match_heartbeat_period("SET @source_heartbeat_period = 1000000000"),
      "1000000000");
}

// A zero period (the replica does not want heartbeats) is a valid value and
// must still be captured so the session can record it.
BOOST_AUTO_TEST_CASE(ZeroPeriodIsCaptured) {
  BOOST_CHECK_EQUAL(match_heartbeat_period("SET @master_heartbeat_period = 0, "
                                           "@source_heartbeat_period = 0"),
                    "0");
}

// Matching is case-insensitive and tolerant of the whitespace around '='.
BOOST_AUTO_TEST_CASE(CaseInsensitiveAndSpacingTolerant) {
  BOOST_CHECK_EQUAL(match_heartbeat_period("set @MASTER_HEARTBEAT_PERIOD=42"),
                    "42");
}

// Unrelated handshake statements that merely mention similarly named variables
// must not be mistaken for a heartbeat-period request.
BOOST_AUTO_TEST_CASE(ChecksumStatementsDoNotMatch) {
  BOOST_CHECK(match_heartbeat_period("SET @source_binlog_checksum = 'CRC32', "
                                     "@master_binlog_checksum = 'CRC32'")
                  .empty());
  BOOST_CHECK(match_heartbeat_period("SELECT @master_binlog_checksum").empty());
}

// The variable name alone, without a numeric value, is not a match.
BOOST_AUTO_TEST_CASE(MissingNumericValueDoesNotMatch) {
  BOOST_CHECK(
      match_heartbeat_period("SET @master_heartbeat_period = ").empty());
  BOOST_CHECK(
      match_heartbeat_period("SELECT @master_heartbeat_period").empty());
}

// The no-op handshake statements must keep matching their patterns: if they
// stopped being recognized they would get a syntax error instead of an OK and
// break the replica handshake.
BOOST_AUTO_TEST_CASE(SetNamesIsRecognized) {
  BOOST_CHECK(matches(minimysql::set_names_query_pattern, "SET NAMES utf8mb4"));
  BOOST_CHECK(matches(minimysql::set_names_query_pattern,
                      "set names latin1 COLLATE latin1_swedish_ci"));
  BOOST_CHECK(!matches(minimysql::set_names_query_pattern, "SELECT VERSION()"));
}

BOOST_AUTO_TEST_CASE(ReplicaUuidVariantsAreRecognized) {
  BOOST_CHECK(matches(minimysql::slave_uuid_query_pattern,
                      "SET @slave_uuid = '5f0e...'"));
  BOOST_CHECK(matches(minimysql::replica_uuid_query_pattern,
                      "SET @replica_uuid = '5f0e...'"));
  // The slave/replica spellings are distinct patterns and must not match each
  // other's variable.
  BOOST_CHECK(!matches(minimysql::slave_uuid_query_pattern,
                       "SET @replica_uuid = '5f0e...'"));
  BOOST_CHECK(!matches(minimysql::replica_uuid_query_pattern,
                       "SET @slave_uuid = '5f0e...'"));
}
