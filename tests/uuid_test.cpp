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

#define BOOST_TEST_MODULE UuisTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/lexical_cast.hpp>

#include <boost/test/unit_test_suite.hpp>

#include <boost/test/tools/old/interface.hpp>

#include "binsrv/gtids/uuid.hpp"

BOOST_AUTO_TEST_CASE(UuidDefaultConstruction) {
  const binsrv::gtids::uuid empty_uuid{};

  BOOST_CHECK(empty_uuid.is_empty());
}

BOOST_AUTO_TEST_CASE(UuidEmptyStringViewConstruction) {
  const std::string_view empty_sv{};
  BOOST_CHECK_THROW(binsrv::gtids::uuid{empty_sv}, std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(UuidZeroStringViewConstruction) {
  const std::string_view zero_sv{"00000000-0000-0000-0000-000000000000"};
  const binsrv::gtids::uuid zero_sv_uuid{zero_sv};

  BOOST_CHECK(zero_sv_uuid.is_empty());
}

BOOST_AUTO_TEST_CASE(UuidValidStringViewConstruction) {
  const std::string_view valid_sv{"11111111-1111-1111-1111-111111111111"};
  const binsrv::gtids::uuid valid_sv_uuid{valid_sv};

  BOOST_CHECK(!valid_sv_uuid.is_empty());
}

BOOST_AUTO_TEST_CASE(UuidInvalidStringViewConstruction) {

  BOOST_CHECK_NO_THROW(
      binsrv::gtids::uuid{"11111111-1111-1111-1111-111111111111"});
  BOOST_CHECK_NO_THROW(binsrv::gtids::uuid{"11111111111111111111111111111111"});
  BOOST_CHECK_NO_THROW(
      binsrv::gtids::uuid{"{11111111-1111-1111-1111-111111111111}"});
  BOOST_CHECK_NO_THROW(
      binsrv::gtids::uuid{"{11111111111111111111111111111111}"});

  BOOST_CHECK_THROW(binsrv::gtids::uuid{"11111111-1111-1111-1111-1111111111"},
                    std::invalid_argument);
  BOOST_CHECK_THROW(
      binsrv::gtids::uuid{"11111111-1111-1111-1111-11111111111111"},
      std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::uuid{"11111111-g111-1111-1111-1111111111"},
                    std::invalid_argument);

  BOOST_CHECK_THROW(
      binsrv::gtids::uuid{"{11111111-1111-1111-1111-111111111111"},
      std::invalid_argument);
  BOOST_CHECK_THROW(
      binsrv::gtids::uuid{"11111111-1111-1111-1111-111111111111}"},
      std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::uuid{"11111111+1111-1111-1111-111111111111"},
                    std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::uuid{"1111-11111111-1111-1111-111111111111"},
                    std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(UuidComparison) {
  const binsrv::gtids::uuid empty_uuid{};
  const binsrv::gtids::uuid first_uuid{"11111111-1111-1111-1111-111111111111"};
  const binsrv::gtids::uuid second_uuid{"22222222-2222-2222-2222-222222222222"};

  BOOST_CHECK_EQUAL(empty_uuid, empty_uuid);
  BOOST_CHECK_EQUAL(first_uuid, first_uuid);

  BOOST_CHECK_NE(empty_uuid, first_uuid);
  BOOST_CHECK_NE(first_uuid, empty_uuid);
  BOOST_CHECK_NE(first_uuid, second_uuid);
  BOOST_CHECK_NE(second_uuid, first_uuid);

  BOOST_CHECK_LT(empty_uuid, first_uuid);
  BOOST_CHECK_LT(first_uuid, second_uuid);

  BOOST_CHECK_LE(empty_uuid, empty_uuid);
  BOOST_CHECK_LE(empty_uuid, first_uuid);
  BOOST_CHECK_LE(first_uuid, first_uuid);
  BOOST_CHECK_LE(first_uuid, second_uuid);
}

BOOST_AUTO_TEST_CASE(GtidSetOstreamOperator) {
  const std::string_view zero_uuid_sv{"00000000-0000-0000-0000-000000000000"};
  const std::string_view first_uuid_sv{"11111111-1111-1111-1111-111111111111"};
  const std::string_view second_uuid_sv{"22222222-2222-2222-2222-222222222222"};

  const binsrv::gtids::uuid empty_uuid{};
  const binsrv::gtids::uuid zero_uuid{zero_uuid_sv};
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};

  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(empty_uuid), zero_uuid_sv);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(zero_uuid), zero_uuid_sv);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(first_uuid),
                    first_uuid_sv);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(second_uuid),
                    second_uuid_sv);
}
