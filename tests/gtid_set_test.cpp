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
#include <utility>

#include <boost/lexical_cast.hpp>

// needed for binsrv::gtids::gtid_set_storage
#include <boost/container/small_vector.hpp> // IWYU pragma: keep

#define BOOST_TEST_MODULE GtidSetTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/unit_test_suite.hpp>

#include <boost/test/tools/old/interface.hpp>

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid.hpp"
#include "binsrv/gtids/gtid_set.hpp"
#include "binsrv/gtids/tag.hpp"

#include "util/byte_span_fwd.hpp"

static constexpr std::string_view first_uuid_sv{
    "11111111-aaaa-1111-aaaa-111111111111"};
static constexpr std::string_view second_uuid_sv{
    "22222222-bbbb-2222-bbbb-222222222222"};
static constexpr std::string_view third_uuid_sv{
    "33333333-cccc-3333-cccc-333333333333"};

static constexpr std::string_view first_tag_sv{"alpha"};
static constexpr std::string_view second_tag_sv{"beta"};

BOOST_AUTO_TEST_CASE(GtidSetDefaultConstruction) {
  const binsrv::gtids::gtid_set empty_gtid_set{};

  BOOST_CHECK(empty_gtid_set.is_empty());
}

BOOST_AUTO_TEST_CASE(GtidSetCopyConstruction) {
  binsrv::gtids::gtid_set gtids{};
  gtids += binsrv::gtids::gtid{binsrv::gtids::uuid{first_uuid_sv}, 1ULL};

  const binsrv::gtids::gtid_set copy{gtids};

  BOOST_CHECK_EQUAL(gtids, copy);
}

BOOST_AUTO_TEST_CASE(GtidSetMoveConstruction) {
  binsrv::gtids::gtid_set gtids{};
  gtids += binsrv::gtids::gtid{binsrv::gtids::uuid{first_uuid_sv}, 1ULL};

  const binsrv::gtids::gtid_set copy{std::move(gtids)};
  BOOST_CHECK(!copy.is_empty());
}

BOOST_AUTO_TEST_CASE(GtidSetCopyAssignmentOperator) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};

  binsrv::gtids::gtid_set gtids{};
  gtids += binsrv::gtids::gtid{first_uuid, 1ULL};

  binsrv::gtids::gtid_set copy{};
  BOOST_CHECK_NE(gtids, copy);

  copy += binsrv::gtids::gtid{first_uuid, 2ULL};
  BOOST_CHECK_NE(gtids, copy);

  copy = gtids;

  BOOST_CHECK_EQUAL(gtids, copy);
}

BOOST_AUTO_TEST_CASE(GtidSetMoveAssignmentOperator) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};

  binsrv::gtids::gtid_set gtids{};
  gtids += binsrv::gtids::gtid{first_uuid, 1ULL};

  binsrv::gtids::gtid_set copy{};
  BOOST_CHECK_NE(gtids, copy);

  copy += binsrv::gtids::gtid{first_uuid, 2ULL};
  BOOST_CHECK_NE(gtids, copy);

  copy = std::move(gtids);

  BOOST_CHECK(!copy.is_empty());
}

BOOST_AUTO_TEST_CASE(GtidSetContains) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};
  const binsrv::gtids::uuid third_uuid{third_uuid_sv};

  const binsrv::gtids::tag first_tag{first_tag_sv};
  const binsrv::gtids::tag second_tag{second_tag_sv};

  binsrv::gtids::gtid_set gtids{};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 1ULL};
  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 2ULL};
  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 1ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 2ULL};
  gtids += binsrv::gtids::gtid{third_uuid, 1ULL};

  BOOST_CHECK(!gtids.contains(binsrv::gtids::gtid{}));
  BOOST_CHECK(gtids.contains(binsrv::gtids::gtid{first_uuid, first_tag, 1ULL}));
  BOOST_CHECK(
      !gtids.contains(binsrv::gtids::gtid{first_uuid, first_tag, 2ULL}));
  BOOST_CHECK(
      !gtids.contains(binsrv::gtids::gtid{first_uuid, second_tag, 1ULL}));
  BOOST_CHECK(
      gtids.contains(binsrv::gtids::gtid{first_uuid, second_tag, 2ULL}));

  BOOST_CHECK(
      gtids.contains(binsrv::gtids::gtid{second_uuid, first_tag, 1ULL}));
  BOOST_CHECK(
      !gtids.contains(binsrv::gtids::gtid{second_uuid, first_tag, 2ULL}));
  BOOST_CHECK(
      !gtids.contains(binsrv::gtids::gtid{second_uuid, second_tag, 1ULL}));
  BOOST_CHECK(
      gtids.contains(binsrv::gtids::gtid{second_uuid, second_tag, 2ULL}));

  BOOST_CHECK(gtids.contains(binsrv::gtids::gtid{third_uuid, 1ULL}));
  BOOST_CHECK(!gtids.contains(binsrv::gtids::gtid{third_uuid, 2ULL}));
}

BOOST_AUTO_TEST_CASE(GtidSetOperatorPlus) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};
  const binsrv::gtids::uuid third_uuid{third_uuid_sv};

  const binsrv::gtids::gtid_set empty_gtids{};
  binsrv::gtids::gtid_set first_gtids{};

  BOOST_CHECK_THROW(first_gtids += binsrv::gtids::gtid{},
                    std::invalid_argument);

  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  first_gtids += binsrv::gtids::gtid{first_uuid, 1ULL};
  first_gtids += binsrv::gtids::gtid{first_uuid, 3ULL};
  first_gtids += binsrv::gtids::gtid{second_uuid, 1ULL};
  first_gtids += binsrv::gtids::gtid{second_uuid, 2ULL};
  first_gtids += binsrv::gtids::gtid{second_uuid, 3ULL};
  first_gtids += binsrv::gtids::gtid{second_uuid, 5ULL};

  BOOST_CHECK_EQUAL(first_gtids + binsrv::gtids::gtid(first_uuid, 1ULL),
                    first_gtids);
  BOOST_CHECK_EQUAL(first_gtids + binsrv::gtids::gtid(second_uuid, 1ULL),
                    first_gtids);

  BOOST_CHECK_EQUAL(first_gtids + empty_gtids, first_gtids);
  BOOST_CHECK_EQUAL(empty_gtids + first_gtids, first_gtids);
  BOOST_CHECK_EQUAL(first_gtids + first_gtids, first_gtids);

  binsrv::gtids::gtid_set second_gtids{};
  second_gtids += binsrv::gtids::gtid{second_uuid, 3ULL};
  second_gtids += binsrv::gtids::gtid{second_uuid, 4ULL};
  second_gtids += binsrv::gtids::gtid{second_uuid, 5ULL};
  second_gtids += binsrv::gtids::gtid{second_uuid, 6ULL};
  second_gtids += binsrv::gtids::gtid{third_uuid, 1ULL};
  second_gtids += binsrv::gtids::gtid{third_uuid, 3ULL};
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

  const auto merged_gtids{first_gtids + second_gtids};
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(merged_gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:1:3, "
                    "22222222-bbbb-2222-bbbb-222222222222:1-6, "
                    "33333333-cccc-3333-cccc-333333333333:1:3");

  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(
                        binsrv::gtids::gtid{first_uuid, 2ULL} + merged_gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:1-3, "
                    "22222222-bbbb-2222-bbbb-222222222222:1-6, "
                    "33333333-cccc-3333-cccc-333333333333:1:3");

  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(
                        binsrv::gtids::gtid{first_uuid, 2ULL} + merged_gtids +
                        binsrv::gtids::gtid{third_uuid, 2ULL}),
                    "11111111-aaaa-1111-aaaa-111111111111:1-3, "
                    "22222222-bbbb-2222-bbbb-222222222222:1-6, "
                    "33333333-cccc-3333-cccc-333333333333:1-3");
}

BOOST_AUTO_TEST_CASE(GtidSetOperatorMinus) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};
  const binsrv::gtids::uuid third_uuid{third_uuid_sv};

  const binsrv::gtids::gtid_set empty_gtids{};
  binsrv::gtids::gtid_set first_gtids{std::string{first_uuid_sv} + ":1:3, " +
                                      std::string{second_uuid_sv} + ":1-3:5"};

  BOOST_CHECK_THROW(first_gtids -= binsrv::gtids::gtid{},
                    std::invalid_argument);

  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

  BOOST_CHECK_EQUAL(first_gtids - binsrv::gtids::gtid(first_uuid, 10ULL),
                    first_gtids);
  BOOST_CHECK_EQUAL(first_gtids - binsrv::gtids::gtid(second_uuid, 10ULL),
                    first_gtids);

  BOOST_CHECK_EQUAL(first_gtids - empty_gtids, first_gtids);
  BOOST_CHECK_EQUAL(empty_gtids - first_gtids, empty_gtids);
  BOOST_CHECK_EQUAL(first_gtids - first_gtids, empty_gtids);

  const binsrv::gtids::gtid_set second_gtids{
      std::string{second_uuid_sv} + ":3-6, " + std::string{third_uuid_sv} +
      ":1:3"};

  const binsrv::gtids::gtid_set merged_gtids{
      std::string{first_uuid_sv} + ":1:3, " + std::string{second_uuid_sv} +
      ":1-6, " + std::string{third_uuid_sv} + ":1:3"};

  BOOST_CHECK_EQUAL(
      merged_gtids - first_gtids,
      binsrv::gtids::gtid_set{"22222222-bbbb-2222-bbbb-222222222222:4:6, "
                              "33333333-cccc-3333-cccc-333333333333:1:3"});
  BOOST_CHECK_EQUAL(
      merged_gtids - second_gtids,
      binsrv::gtids::gtid_set{"11111111-aaaa-1111-aaaa-111111111111:1:3, "
                              "22222222-bbbb-2222-bbbb-222222222222:1-2"});

  BOOST_CHECK_EQUAL(
      binsrv::gtids::gtid_set{"11111111-aaaa-1111-aaaa-111111111111:1-3, "
                              "22222222-bbbb-2222-bbbb-222222222222:1-6, "
                              "33333333-cccc-3333-cccc-333333333333:1:3"} -
          merged_gtids,
      binsrv::gtids::gtid(first_uuid, 2ULL));

  BOOST_CHECK_EQUAL(
      binsrv::gtids::gtid_set{"11111111-aaaa-1111-aaaa-111111111111:1-3, "
                              "22222222-bbbb-2222-bbbb-222222222222:1-6, "
                              "33333333-cccc-3333-cccc-333333333333:1-3"} -
          merged_gtids,
      binsrv::gtids::gtid(first_uuid, 2ULL) +
          binsrv::gtids::gtid(third_uuid, 2ULL));
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
}

BOOST_AUTO_TEST_CASE(GtidSetAddIntervalUntagged) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};
  const binsrv::gtids::tag empty_tag{};

  binsrv::gtids::gtid_set gtids{};
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids.add_interval(first_uuid, empty_tag, 2ULL, 4ULL);
  gtids.add_interval(first_uuid, empty_tag, 6ULL, 8ULL);

  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:2-4:6-8");

  gtids.add_interval(first_uuid, empty_tag, 2ULL, 3ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:2-4:6-8");

  gtids.add_interval(first_uuid, empty_tag, 7ULL, 9ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:2-4:6-9");

  gtids.add_interval(first_uuid, empty_tag, 3ULL, 7ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:2-9");

  gtids.add_interval(first_uuid, empty_tag, 1ULL, 10ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:1-10");

  gtids.add_interval(first_uuid, empty_tag, 11ULL, 11ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:1-11");

  gtids.add_interval(second_uuid, empty_tag, 1ULL, 11ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:1-11, "
                    "22222222-bbbb-2222-bbbb-222222222222:1-11");
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
}

BOOST_AUTO_TEST_CASE(GtidSetAddIntervalTagged) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};
  const binsrv::gtids::tag first_tag{first_tag_sv};

  binsrv::gtids::gtid_set gtids{};
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids.add_interval(first_uuid, first_tag, 2ULL, 4ULL);
  gtids.add_interval(first_uuid, first_tag, 6ULL, 8ULL);

  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:2-4:6-8");

  gtids.add_interval(first_uuid, first_tag, 2ULL, 3ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:2-4:6-8");

  gtids.add_interval(first_uuid, first_tag, 7ULL, 9ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:2-4:6-9");

  gtids.add_interval(first_uuid, first_tag, 3ULL, 7ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:2-9");

  gtids.add_interval(first_uuid, first_tag, 1ULL, 10ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:1-10");

  gtids.add_interval(first_uuid, first_tag, 11ULL, 11ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:1-11");

  gtids.add_interval(second_uuid, first_tag, 1ULL, 11ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:1-11, "
                    "22222222-bbbb-2222-bbbb-222222222222:alpha:1-11");
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
}

BOOST_AUTO_TEST_CASE(GtidSetSubtractIntervalUntagged) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};
  const binsrv::gtids::tag empty_tag{};

  binsrv::gtids::gtid_set gtids{std::string{first_uuid_sv} + ":1-11, " +
                                std::string{second_uuid_sv} + ":1-11"};
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids.subtract_interval(second_uuid, empty_tag, 1ULL, 11ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:1-11");

  gtids.subtract_interval(first_uuid, empty_tag, 11ULL, 11ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:1-10");

  gtids.subtract_interval(first_uuid, empty_tag, 1ULL, 1ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:2-10");

  gtids.subtract_interval(first_uuid, empty_tag, 5ULL, 5ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:2-4:6-10");

  gtids.subtract_interval(first_uuid, empty_tag, 8ULL, 12ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:2-4:6-7");

  gtids.subtract_interval(first_uuid, empty_tag, 2ULL, 4ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:6-7");

  gtids.subtract_interval(first_uuid, empty_tag, 4ULL, 9ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids), "");
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
}

BOOST_AUTO_TEST_CASE(GtidSetSubtractIntervalTagged) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};
  const binsrv::gtids::tag first_tag{first_tag_sv};

  binsrv::gtids::gtid_set gtids{
      std::string{first_uuid_sv} + ":" + std::string{first_tag_sv} + ":1-11, " +
      std::string{second_uuid_sv} + ":" + std::string{first_tag_sv} + ":1-11"};
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids.subtract_interval(second_uuid, first_tag, 1ULL, 11ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:1-11");

  gtids.subtract_interval(first_uuid, first_tag, 11ULL, 11ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:1-10");

  gtids.subtract_interval(first_uuid, first_tag, 1ULL, 1ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:2-10");

  gtids.subtract_interval(first_uuid, first_tag, 5ULL, 5ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:2-4:6-10");

  gtids.subtract_interval(first_uuid, first_tag, 8ULL, 12ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:2-4:6-7");

  gtids.subtract_interval(first_uuid, first_tag, 2ULL, 4ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids),
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:6-7");

  gtids.subtract_interval(first_uuid, first_tag, 4ULL, 9ULL);
  BOOST_CHECK_EQUAL(boost::lexical_cast<std::string>(gtids), "");
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
}

BOOST_AUTO_TEST_CASE(GtidSetClear) {
  binsrv::gtids::gtid_set gtids{};
  BOOST_CHECK(gtids.is_empty());

  gtids += binsrv::gtids::gtid{binsrv::gtids::uuid{first_uuid_sv}, 1ULL};
  BOOST_CHECK(!gtids.is_empty());

  gtids.clear();
  BOOST_CHECK(gtids.is_empty());
}

BOOST_AUTO_TEST_CASE(GtidSetContainsTags) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};

  const binsrv::gtids::tag first_tag{first_tag_sv};

  binsrv::gtids::gtid_set gtids{};
  BOOST_CHECK(!gtids.contains_tags());

  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids += binsrv::gtids::gtid{first_uuid, 1ULL};
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  BOOST_CHECK(!gtids.contains_tags());

  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 1ULL};
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  BOOST_CHECK(gtids.contains_tags());
}

class gtid_set_encoding_fixture {
protected:
  static void check_encoding_roundtrip(const binsrv::gtids::gtid_set &gtids) {
    const auto encoded_size{gtids.calculate_encoded_size()};
    binsrv::gtids::gtid_set_storage buffer(encoded_size);
    util::byte_span destination{buffer};
    BOOST_CHECK_NO_THROW(gtids.encode_to(destination));
    BOOST_CHECK(destination.empty());

    binsrv::gtids::gtid_set restored;
    const util::const_byte_span source{buffer};
    BOOST_CHECK_NO_THROW(restored = binsrv::gtids::gtid_set{source});

    BOOST_CHECK_EQUAL(gtids, restored);
  }
};

BOOST_FIXTURE_TEST_CASE(GtidSetEncodingEmpty, gtid_set_encoding_fixture) {
  const binsrv::gtids::gtid_set gtids{};
  check_encoding_roundtrip(gtids);
}

BOOST_FIXTURE_TEST_CASE(GtidSetEncodingUntagged, gtid_set_encoding_fixture) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};

  binsrv::gtids::gtid_set gtids{};
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids += binsrv::gtids::gtid{first_uuid, 1ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 2ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 3ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 5ULL};

  gtids += binsrv::gtids::gtid{second_uuid, 12ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 13ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 15ULL};
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

  BOOST_CHECK(!gtids.contains_tags());
  check_encoding_roundtrip(gtids);
}

BOOST_FIXTURE_TEST_CASE(GtidSetEncodingTagged, gtid_set_encoding_fixture) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};

  const binsrv::gtids::tag first_tag{first_tag_sv};
  const binsrv::gtids::tag second_tag{second_tag_sv};

  binsrv::gtids::gtid_set gtids{};
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 1ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 2ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 3ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 5ULL};

  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 12ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 13ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 15ULL};
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

  BOOST_CHECK(gtids.contains_tags());
  check_encoding_roundtrip(gtids);
}

BOOST_FIXTURE_TEST_CASE(GtidSetEncodingMixed, gtid_set_encoding_fixture) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};

  const binsrv::gtids::tag first_tag{first_tag_sv};
  const binsrv::gtids::tag second_tag{second_tag_sv};

  binsrv::gtids::gtid_set gtids{};
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids += binsrv::gtids::gtid{first_uuid, 101ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 102ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 103ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 105ULL};

  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 1ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 2ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 3ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 5ULL};

  gtids += binsrv::gtids::gtid{second_uuid, 112ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 113ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 115ULL};

  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 12ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 13ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 15ULL};
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

  BOOST_CHECK(gtids.contains_tags());
  check_encoding_roundtrip(gtids);
}

BOOST_AUTO_TEST_CASE(GtidSetStreamOperatorEmpty) {
  const binsrv::gtids::gtid_set gtids{};

  const auto gtids_str{boost::lexical_cast<std::string>(gtids)};
  BOOST_CHECK_EQUAL(gtids_str, "");
  const auto restored_gtids{
      boost::lexical_cast<binsrv::gtids::gtid_set>(gtids_str)};
  BOOST_CHECK_EQUAL(gtids, restored_gtids);
}

BOOST_AUTO_TEST_CASE(GtidSetStreamOperatorUntagged) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};

  binsrv::gtids::gtid_set gtids{};
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids += binsrv::gtids::gtid{first_uuid, 1ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 2ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 3ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 5ULL};

  gtids += binsrv::gtids::gtid{second_uuid, 11ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 12ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 13ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 15ULL};
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

  const auto gtids_str{boost::lexical_cast<std::string>(gtids)};
  BOOST_CHECK_EQUAL(gtids_str, "11111111-aaaa-1111-aaaa-111111111111:1-3:5, "
                               "22222222-bbbb-2222-bbbb-222222222222:11-13:15");
  const auto restored_gtids{
      boost::lexical_cast<binsrv::gtids::gtid_set>(gtids_str)};
  BOOST_CHECK_EQUAL(gtids, restored_gtids);
}

BOOST_AUTO_TEST_CASE(GtidSetStreamOperatorTagged) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};

  const binsrv::gtids::tag first_tag{first_tag_sv};
  const binsrv::gtids::tag second_tag{second_tag_sv};

  binsrv::gtids::gtid_set gtids{};
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 111ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 112ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 113ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 115ULL};

  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 121ULL};
  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 122ULL};
  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 123ULL};
  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 125ULL};

  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 211ULL};
  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 212ULL};
  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 213ULL};
  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 215ULL};

  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 221ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 222ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 223ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 225ULL};
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

  const auto gtids_str{boost::lexical_cast<std::string>(gtids)};
  BOOST_CHECK_EQUAL(gtids_str,
                    "11111111-aaaa-1111-aaaa-111111111111:alpha:111-113:115:"
                    "beta:121-123:125, "
                    "22222222-bbbb-2222-bbbb-222222222222:alpha:211-213:215:"
                    "beta:221-223:225");
  const auto restored_gtids{
      boost::lexical_cast<binsrv::gtids::gtid_set>(gtids_str)};
  BOOST_CHECK_EQUAL(gtids, restored_gtids);
}

BOOST_AUTO_TEST_CASE(GtidSetStreamOperatorMixed) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};

  const binsrv::gtids::tag first_tag{first_tag_sv};
  const binsrv::gtids::tag second_tag{second_tag_sv};

  binsrv::gtids::gtid_set gtids{};
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids += binsrv::gtids::gtid{first_uuid, 101ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 102ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 103ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 105ULL};

  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 111ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 112ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 113ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 115ULL};

  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 121ULL};
  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 122ULL};
  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 123ULL};
  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 125ULL};

  gtids += binsrv::gtids::gtid{second_uuid, 201ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 202ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 203ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 205ULL};

  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 211ULL};
  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 212ULL};
  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 213ULL};
  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 215ULL};

  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 221ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 222ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 223ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 225ULL};
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

  const auto gtids_str{boost::lexical_cast<std::string>(gtids)};
  BOOST_CHECK_EQUAL(gtids_str,
                    "11111111-aaaa-1111-aaaa-111111111111:101-103:105:alpha:"
                    "111-113:115:beta:121-123:125, "
                    "22222222-bbbb-2222-bbbb-222222222222:201-203:205:alpha:"
                    "211-213:215:beta:221-223:225");
  const auto restored_gtids{
      boost::lexical_cast<binsrv::gtids::gtid_set>(gtids_str)};
  BOOST_CHECK_EQUAL(gtids, restored_gtids);
}

BOOST_AUTO_TEST_CASE(GtidSetWhitespaces) {
  const binsrv::gtids::uuid first_uuid{first_uuid_sv};
  const binsrv::gtids::uuid second_uuid{second_uuid_sv};

  const binsrv::gtids::tag first_tag{first_tag_sv};
  const binsrv::gtids::tag second_tag{second_tag_sv};

  binsrv::gtids::gtid_set gtids{};
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  gtids += binsrv::gtids::gtid{first_uuid, 101ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 102ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 103ULL};
  gtids += binsrv::gtids::gtid{first_uuid, 105ULL};

  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 111ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 112ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 113ULL};
  gtids += binsrv::gtids::gtid{first_uuid, first_tag, 115ULL};

  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 121ULL};
  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 122ULL};
  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 123ULL};
  gtids += binsrv::gtids::gtid{first_uuid, second_tag, 125ULL};

  gtids += binsrv::gtids::gtid{second_uuid, 201ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 202ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 203ULL};
  gtids += binsrv::gtids::gtid{second_uuid, 205ULL};

  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 211ULL};
  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 212ULL};
  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 213ULL};
  gtids += binsrv::gtids::gtid{second_uuid, first_tag, 215ULL};

  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 221ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 222ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 223ULL};
  gtids += binsrv::gtids::gtid{second_uuid, second_tag, 225ULL};
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

  auto gtids_str{boost::lexical_cast<std::string>(gtids)};
  // erasing all space characters from the GTIDs string

  // looks like a misconfiguration in clang-tidy-19 that doesn't know that
  // std::erase() for std::string is located in the <string> system header

  // TODO: re-check this when switching to clang-20

  // NOLINTNEXTLINE(misc-include-cleaner)
  std::erase(gtids_str, ' ');
  BOOST_CHECK(gtids_str.find(' ') == std::string::npos);
  const auto restored_gtids{
      boost::lexical_cast<binsrv::gtids::gtid_set>(gtids_str)};
  BOOST_CHECK_EQUAL(gtids, restored_gtids);
}
