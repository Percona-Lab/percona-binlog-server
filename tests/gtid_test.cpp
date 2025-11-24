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

#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/lexical_cast.hpp>

#define BOOST_TEST_MODULE GtidTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/unit_test_suite.hpp>

#include <boost/test/tools/old/interface.hpp>

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid.hpp"

static constexpr binsrv::gtids::uuid uuid_sample{
    {0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87, 0x78, 0x69, 0x5a, 0x4b,
     0x3c, 0x2d, 0x1e, 0x0f}};
static constexpr binsrv::gtids::gno_t gno_sample{42ULL};

BOOST_AUTO_TEST_CASE(GtidDefaultConstruction) {
  const binsrv::gtids::gtid empty_gtid{};

  BOOST_CHECK(empty_gtid.get_uuid().is_nil());
  BOOST_CHECK(empty_gtid.get_tag().is_empty());
  BOOST_CHECK_EQUAL(empty_gtid.get_gno(), 0ULL);
}

BOOST_AUTO_TEST_CASE(GtidThreeArgumentConstruction) {
  const binsrv::gtids::uuid empty_uuid{};
  const binsrv::gtids::uuid hardcoded_uuid{uuid_sample};

  const binsrv::gtids::tag empty_tag{};
  const binsrv::gtids::tag hardcoded_tag{"alpha"};

  const binsrv::gtids::gno_t zero_gno{0ULL};
  const binsrv::gtids::gno_t hardcoded_gno{gno_sample};
  const binsrv::gtids::gno_t too_large_gno{binsrv::gtids::max_gno + 1ULL};

  BOOST_CHECK_THROW(binsrv::gtids::gtid(empty_uuid, empty_tag, zero_gno),
                    std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::gtid(empty_uuid, empty_tag, hardcoded_gno),
                    std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::gtid(empty_uuid, empty_tag, too_large_gno),
                    std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::gtid(empty_uuid, hardcoded_tag, zero_gno),
                    std::invalid_argument);
  BOOST_CHECK_THROW(
      binsrv::gtids::gtid(empty_uuid, hardcoded_tag, hardcoded_gno),
      std::invalid_argument);
  BOOST_CHECK_THROW(
      binsrv::gtids::gtid(empty_uuid, hardcoded_tag, too_large_gno),
      std::invalid_argument);

  BOOST_CHECK_THROW(binsrv::gtids::gtid(hardcoded_uuid, empty_tag, zero_gno),
                    std::invalid_argument);
  BOOST_CHECK_NO_THROW(
      binsrv::gtids::gtid(hardcoded_uuid, empty_tag, hardcoded_gno));
  BOOST_CHECK_THROW(
      binsrv::gtids::gtid(hardcoded_uuid, empty_tag, too_large_gno),
      std::invalid_argument);
  BOOST_CHECK_THROW(
      binsrv::gtids::gtid(hardcoded_uuid, hardcoded_tag, zero_gno),
      std::invalid_argument);
  BOOST_CHECK_NO_THROW(
      binsrv::gtids::gtid(hardcoded_uuid, hardcoded_tag, hardcoded_gno));
  BOOST_CHECK_THROW(
      binsrv::gtids::gtid(hardcoded_uuid, hardcoded_tag, too_large_gno),
      std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(GtidTwoArgumentConstruction) {
  const binsrv::gtids::uuid empty_uuid{};
  const binsrv::gtids::uuid hardcoded_uuid{uuid_sample};

  const binsrv::gtids::gno_t zero_gno{0ULL};
  const binsrv::gtids::gno_t hardcoded_gno{gno_sample};
  const binsrv::gtids::gno_t too_large_gno{binsrv::gtids::max_gno + 1ULL};

  BOOST_CHECK_THROW(binsrv::gtids::gtid(empty_uuid, zero_gno),
                    std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::gtid(empty_uuid, hardcoded_gno),
                    std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::gtid(empty_uuid, too_large_gno),
                    std::invalid_argument);

  BOOST_CHECK_THROW(binsrv::gtids::gtid(hardcoded_uuid, zero_gno),
                    std::invalid_argument);
  BOOST_CHECK_NO_THROW(binsrv::gtids::gtid(hardcoded_uuid, hardcoded_gno));
  BOOST_CHECK_THROW(binsrv::gtids::gtid(hardcoded_uuid, too_large_gno),
                    std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(GtidOstreamOperator) {
  const binsrv::gtids::gtid empty_gtid{};
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(empty_gtid),
                    "00000000-0000-0000-0000-000000000000:0");

  const binsrv::gtids::gtid non_tagged_gtid{uuid_sample, gno_sample};
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(non_tagged_gtid),
                    "f0e1d2c3-b4a5-9687-7869-5a4b3c2d1e0f:42");
}
