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

#ifndef MINIMYSQL_HANDSHAKE_QUERY_PATTERNS_HPP
#define MINIMYSQL_HANDSHAKE_QUERY_PATTERNS_HPP

#include <string_view>

namespace minimysql {

// Regular expressions (ECMAScript grammar, intended to be compiled
// case-insensitively) that recognize the SQL statements a replica sends during
// the replication handshake which cannot be matched against a fixed literal -
// either because they carry a value that varies between invocations (and that
// we need to capture) or because the replica sends them with a trailing
// argument we do not care about. Statements with a fixed text are matched
// exactly instead and are not listed here.

// Recognizes the replica's request to configure the replication heartbeat
// period and captures the period, a decimal count of nanoseconds, in capture
// group 1.
//
// A replica that runs
//   CHANGE REPLICATION SOURCE ... SOURCE_HEARTBEAT_PERIOD = <seconds>
// with a non-zero period assigns the same nanosecond value to both the legacy
// and the modern user variable in a single statement, e.g.
//   SET @master_heartbeat_period = 30000000000, @source_heartbeat_period =
//   30000000000
// (no SESSION keyword: these are user variables, not system variables). The
// value is an unsigned integer number of nanoseconds; a period of zero means
// the replica does not want heartbeats. Both assignments always carry the same
// value, so matching the first one is sufficient.
inline constexpr std::string_view heartbeat_period_query_pattern{
    R"(^SET\s+@(?:master|source)_heartbeat_period\s*=\s*([0-9]+))"};

// Recognizes 'SET NAMES <charset>[ COLLATE <collation>]', sent by
// libmysqlclient when the replica opens the connection to initialize the
// session charset. PBS only speaks the binary binlog event stream, so the
// session charset has no observable effect and the statement is accepted as a
// no-op.
inline constexpr std::string_view set_names_query_pattern{R"(^SET\s+NAMES\b)"};

// Recognizes 'SET @slave_uuid = <uuid>', by which the replica tells the source
// its own uuid for anti-loop reporting and for the SHOW REPLICAS view. PBS
// exposes neither today, so the value is accepted and discarded.
// TODO: when PBS gains SHOW REPLICAS / a connected-replicas view (or needs the
// uuid for duplicate-replica detection), capture and store the value instead
// of discarding it.
inline constexpr std::string_view slave_uuid_query_pattern{
    R"(^SET\s+@slave_uuid\b)"};

// Recognizes the modern spelling 'SET @replica_uuid = <uuid>'. Same meaning
// and same no-op handling as 'slave_uuid_query_pattern'.
inline constexpr std::string_view replica_uuid_query_pattern{
    R"(^SET\s+@replica_uuid\b)"};

} // namespace minimysql

#endif // MINIMYSQL_HANDSHAKE_QUERY_PATTERNS_HPP
