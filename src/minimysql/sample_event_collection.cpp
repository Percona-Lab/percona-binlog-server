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

#include "minimysql/sample_event_collection.hpp"

#include <string>
#include <string_view>

#include <boost/algorithm/hex.hpp>

namespace minimysql {

namespace {

constexpr std::string_view rotate_artificial{
    "00 00 00 00 04 01 00 00 00 28 00 00 00 00 00 00"
    "00 20 00 04 00 00 00 00 00 00 00 62 69 6e 6c 6f"
    "67 2e 30 30 30 30 30 31                        "};
constexpr std::string_view fde{
    "1a b2 1c 6a 0f 01 00 00 00 7b 00 00 00 7f 00 00"
    "00 00 00 04 00 38 2e 34 2e 37 2d 37 00 00 00 00"
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
    "00 00 00 00 00 00 00 1a b2 1c 6a 13 00 0d 00 08"
    "00 00 00 00 04 00 04 00 00 00 63 00 04 1a 08 00"
    "00 00 00 00 00 02 00 00 00 0a 0a 0a 2a 2a 00 12"
    "34 00 0a 28 00 00 01 ee c6 67 82               "};
constexpr std::string_view previous_gtids{
    "1a b2 1c 6a 23 01 00 00 00 1f 00 00 00 9e 00 00"
    "00 80 00 00 00 00 00 00 00 00 00 09 5d aa 16   "};
constexpr std::string_view first_gtid_log{
    "52 b2 1c 6a 21 01 00 00 00 4d 00 00 00 eb 00 00"
    "00 00 00 01 b1 14 55 0c 5d 3d 11 f1 a7 3a 00 15"
    "5d f1 f4 92 01 00 00 00 00 00 00 00 02 00 00 00"
    "00 00 00 00 00 01 00 00 00 00 00 00 00 9c 7e f6"
    "5f 24 53 06 cb 17 3a 01 00 80 dc f9 5c         "};
constexpr std::string_view first_query{
    "52 b2 1c 6a 02 01 00 00 00 7e 00 00 00 69 01 00"
    "00 00 00 09 00 00 00 00 00 00 00 04 00 00 2f 00"
    "00 00 00 00 00 01 20 00 a0 45 00 00 00 00 06 03"
    "73 74 64 04 ff 00 ff 00 ff 00 0c 01 74 65 73 74"
    "00 11 06 00 00 00 00 00 00 00 12 ff 00 13 00 74"
    "65 73 74 00 43 52 45 41 54 45 20 54 41 42 4c 45"
    "20 74 31 28 69 64 20 53 45 52 49 41 4c 20 50 52"
    "49 4d 41 52 59 20 4b 45 59 29 d9 1d 2b 2b      "};
constexpr std::string_view second_gtid_log{
    "57 b2 1c 6a 21 01 00 00 00 4f 00 00 00 b8 01 00"
    "00 00 00 00 b1 14 55 0c 5d 3d 11 f1 a7 3a 00 15"
    "5d f1 f4 92 02 00 00 00 00 00 00 00 02 01 00 00"
    "00 00 00 00 00 02 00 00 00 00 00 00 00 8a 35 4f"
    "60 24 53 06 fc 15 01 17 3a 01 00 24 35 52 71   "};
constexpr std::string_view second_query{
    "57 b2 1c 6a 02 01 00 00 00 4b 00 00 00 03 02 00"
    "00 08 00 09 00 00 00 00 00 00 00 04 00 00 1d 00"
    "00 00 00 00 00 01 20 00 a0 45 00 00 00 00 06 03"
    "73 74 64 04 ff 00 ff 00 ff 00 12 ff 00 74 65 73"
    "74 00 42 45 47 49 4e c8 4c ce 80               "};
constexpr std::string_view second_table_map{
    "57 b2 1c 6a 13 01 00 00 00 30 00 00 00 33 02 00"
    "00 00 00 73 00 00 00 00 00 01 00 04 74 65 73 74"
    "00 02 74 31 00 01 08 00 00 01 01 80 21 d6 bc 8c"};
constexpr std::string_view second_write_rows{
    "57 b2 1c 6a 1e 01 00 00 00 2c 00 00 00 5f 02 00"
    "00 00 00 73 00 00 00 00 00 01 00 02 00 01 ff 00"
    "01 00 00 00 00 00 00 00 82 65 a7 86            "};
constexpr std::string_view second_xid{
    "57 b2 1c 6a 10 01 00 00 00 1f 00 00 00 7e 02 00"
    "00 00 00 07 00 00 00 00 00 00 00 e3 36 06 8f   "};

std::string hex_to_bin(std::string_view hex) {
  std::string filtered_hex{hex};
  std::erase(filtered_hex, ' ');
  return boost::algorithm::unhex(filtered_hex);
}

} // anonymous namespace

sample_event_collection::sample_event_collection()
    : events_{hex_to_bin(rotate_artificial), hex_to_bin(fde),
              hex_to_bin(previous_gtids),    hex_to_bin(first_gtid_log),
              hex_to_bin(first_query),       hex_to_bin(second_gtid_log),
              hex_to_bin(second_query),      hex_to_bin(second_table_map),
              hex_to_bin(second_write_rows), hex_to_bin(second_xid)} {}

} // namespace minimysql
