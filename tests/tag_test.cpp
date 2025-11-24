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

#define BOOST_TEST_MODULE TagTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/unit_test_suite.hpp>

#include <boost/test/tools/old/interface.hpp>

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/tag.hpp"

BOOST_AUTO_TEST_CASE(TagDefaultConstruction) {
  const binsrv::gtids::tag empty_tag{};

  BOOST_CHECK(empty_tag.is_empty());

  const auto name{empty_tag.get_name()};
  BOOST_CHECK(name.empty());
}

BOOST_AUTO_TEST_CASE(TagEmptyStringViewConstruction) {
  const std::string_view empty_sv{};
  const binsrv::gtids::tag empty_sv_tag{empty_sv};

  BOOST_CHECK(empty_sv_tag.is_empty());

  const auto name{empty_sv_tag.get_name()};
  BOOST_CHECK(name.empty());
}

BOOST_AUTO_TEST_CASE(TagValidStringViewConstruction) {
  const std::string_view valid_sv{"alpha"};
  const binsrv::gtids::tag valid_sv_tag{valid_sv};

  BOOST_CHECK(!valid_sv_tag.is_empty());

  const auto name{valid_sv_tag.get_name()};
  BOOST_CHECK_EQUAL(name, valid_sv);
}

BOOST_AUTO_TEST_CASE(TagInvalidStringViewConstruction) {
  BOOST_CHECK_THROW(binsrv::gtids::tag{"$"}, std::invalid_argument);
  BOOST_CHECK_NO_THROW(binsrv::gtids::tag{"_"});
  BOOST_CHECK_NO_THROW(binsrv::gtids::tag{"a"});
  BOOST_CHECK_THROW(binsrv::gtids::tag{"0"}, std::invalid_argument);

  BOOST_CHECK_THROW(binsrv::gtids::tag{"$$"}, std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::tag{"$_"}, std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::tag{"$a"}, std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::tag{"$0"}, std::invalid_argument);

  BOOST_CHECK_THROW(binsrv::gtids::tag{"_$"}, std::invalid_argument);
  BOOST_CHECK_NO_THROW(binsrv::gtids::tag{"__"});
  BOOST_CHECK_NO_THROW(binsrv::gtids::tag{"_a"});
  BOOST_CHECK_NO_THROW(binsrv::gtids::tag{"_0"});

  BOOST_CHECK_THROW(binsrv::gtids::tag{"a$"}, std::invalid_argument);
  BOOST_CHECK_NO_THROW(binsrv::gtids::tag{"a_"});
  BOOST_CHECK_NO_THROW(binsrv::gtids::tag{"aa"});
  BOOST_CHECK_NO_THROW(binsrv::gtids::tag{"a0"});

  BOOST_CHECK_THROW(binsrv::gtids::tag{"0$"}, std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::tag{"0_"}, std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::tag{"0a"}, std::invalid_argument);
  BOOST_CHECK_THROW(binsrv::gtids::tag{"00"}, std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(TagLongStringViewConstruction) {
  std::string long_name(binsrv::gtids::tag_max_length, 'a');
  const binsrv::gtids::tag valid_sv_tag{long_name};

  BOOST_CHECK(!valid_sv_tag.is_empty());

  const auto name{valid_sv_tag.get_name()};
  BOOST_CHECK_EQUAL(name, long_name);

  long_name += 'a';
  BOOST_CHECK_THROW(binsrv::gtids::tag{long_name}, std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(TagComparison) {
  const binsrv::gtids::tag empty_tag{};
  const binsrv::gtids::tag alpha_tag{"alpha"};
  const binsrv::gtids::tag beta_tag{"beta"};

  BOOST_CHECK_EQUAL(empty_tag, empty_tag);
  BOOST_CHECK_EQUAL(alpha_tag, alpha_tag);

  BOOST_CHECK_NE(empty_tag, alpha_tag);
  BOOST_CHECK_NE(alpha_tag, empty_tag);
  BOOST_CHECK_NE(alpha_tag, beta_tag);
  BOOST_CHECK_NE(beta_tag, alpha_tag);

  BOOST_CHECK_LT(empty_tag, alpha_tag);
  BOOST_CHECK_LT(alpha_tag, beta_tag);

  BOOST_CHECK_LE(empty_tag, empty_tag);
  BOOST_CHECK_LE(empty_tag, alpha_tag);
  BOOST_CHECK_LE(alpha_tag, alpha_tag);
  BOOST_CHECK_LE(alpha_tag, beta_tag);
}
