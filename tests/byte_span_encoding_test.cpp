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

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#define BOOST_TEST_MODULE TagTests
// this include is needed as it provides the 'main()' function
// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/test/unit_test.hpp>

#include <boost/test/unit_test_log.hpp>
#include <boost/test/unit_test_suite.hpp>

#include <boost/test/tools/old/interface.hpp>

#include <boost/mpl/list.hpp>

#include "util/byte_span_extractors.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/byte_span_inserters.hpp"

struct mixin_fixture {
  static constexpr std::size_t bits_in_byte{
      std::numeric_limits<std::uint8_t>::digits};
  // varlen int uses 1 byte for each 7 bits
  static constexpr std::size_t bit_group_size{bits_in_byte - 1U};

  // the number of 7-bit groups in the given type
  template <typename ValueType>
  static constexpr std::size_t number_of_iterations =
      sizeof(ValueType) * bits_in_byte / bit_group_size;

  // an auxiliary method that performs varlen int encoding / decoding of the
  // provided value and compares the values
  template <typename ValueType> static void check_roundtrip(ValueType value) {
    const auto calculated_encoded_size{util::calculate_varlen_int_size(value)};
    using encoding_buffer_type =
        std::array<std::byte, sizeof(std::uint64_t) + 1U>;

    encoding_buffer_type buffer;
    util::byte_span encoding_portion{buffer};
    BOOST_CHECK(
        util::insert_varlen_int_to_byte_span_checked(encoding_portion, value));

    const auto actual_encoded_size{std::size(buffer) -
                                   std::size(encoding_portion)};

    BOOST_CHECK_EQUAL(actual_encoded_size, calculated_encoded_size);

    util::const_byte_span decoding_portion{buffer};
    ValueType restored_value;
    BOOST_CHECK(util::extract_varlen_int_from_byte_span_checked(
        decoding_portion, restored_value));

    const auto actual_decoded_size{std::size(buffer) -
                                   std::size(decoding_portion)};
    BOOST_CHECK_EQUAL(actual_decoded_size, calculated_encoded_size);

    BOOST_CHECK_EQUAL(restored_value, value);
  }

  template <typename ValueType>
  static std::pair<ValueType, ValueType>
  calculate_group_min_max_for_iteration(std::size_t iteration) {
    ValueType group_min;
    ValueType group_max;
    if constexpr (std::is_unsigned_v<ValueType>) {
      // at the very first iteration the min value is always 0
      group_min = static_cast<ValueType>(1ULL << (bit_group_size * iteration));
      if (iteration == number_of_iterations<ValueType>) {
        // for the last iteration the max value is the max value for the given
        // type - 1
        group_max = std::numeric_limits<ValueType>::max();
      } else {
        group_max =
            static_cast<ValueType>(1ULL << (bit_group_size * (iteration + 1U)));
      }
      --group_max;
    } else {
      // at the very first iteration the min value is always 0
      if (iteration == 0U) {
        group_min = static_cast<ValueType>(1LL);
      } else {
        group_min =
            static_cast<ValueType>(1ULL << (bit_group_size * iteration - 1U));
      }

      if (iteration == number_of_iterations<ValueType>) {
        group_max = std::numeric_limits<ValueType>::max();
      } else {
        group_max = static_cast<ValueType>(
            1ULL << (bit_group_size * (iteration + 1U) - 1U));
      }
      --group_max;
    }
    return {group_min, group_max};
  }

  template <typename ValueType>
  using stream_type_t = std::conditional_t<std::is_signed_v<ValueType>,
                                           std::int64_t, std::uint64_t>;

  template <typename ValueType>
  static void print_and_check_single(ValueType value) {
    BOOST_TEST_MESSAGE("  Value "
                       << static_cast<stream_type_t<ValueType>>(value));
    check_roundtrip(value);
  }

  template <typename ValueType> static void print_and_check(ValueType value) {
    print_and_check_single(value);
    if constexpr (std::is_signed_v<ValueType>) {
      const auto negated_value{static_cast<ValueType>(-value)};
      print_and_check_single(negated_value);
    }
  }
};

using varlen_int_encoding_types =
    boost::mpl::list<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t,
                     std::int8_t, std::int16_t, std::int32_t, std::int64_t>;

BOOST_FIXTURE_TEST_CASE_TEMPLATE(VarlenIntEncoding, ValueType,
                                 varlen_int_encoding_types, mixin_fixture) {
  using stream_type = stream_type_t<ValueType>;

  const ValueType delta{16ULL};

  // each iteration represents a new 7-bit gtoup that adds one more byte to the
  // encoding deliberately adding one more iteration for the last (shorter than
  // 7-bit) group
  for (std::size_t iteration{0U}; iteration <= number_of_iterations<ValueType>;
       ++iteration) {
    // calculating min and max values of the group
    auto [group_min, group_max]{
        calculate_group_min_max_for_iteration<ValueType>(iteration)};
    // calculating the midpoint
    const ValueType group_midpoint{std::midpoint(group_min, group_max)};
    BOOST_TEST_MESSAGE("Iteration "
                       << iteration << ", min "
                       << static_cast<stream_type>(group_min) << ", midpoint "
                       << static_cast<stream_type>(group_midpoint) << ", max "
                       << static_cast<stream_type>(group_max));

    // checking values in the [group_min; group_min + delta] range
    for (ValueType current_value{group_min};
         current_value <= static_cast<ValueType>(group_min + delta);
         ++current_value) {
      print_and_check(current_value);
    }

    // checking values in the [group_midpoint - delta; group_midpoint + delta]
    // range
    for (ValueType current_value{
             static_cast<ValueType>(group_midpoint - delta)};
         current_value <= static_cast<ValueType>(group_midpoint + delta);
         ++current_value) {
      print_and_check(current_value);
    }

    // checking values in the [group_max - delta; group_max] range
    for (ValueType current_value{static_cast<ValueType>(group_max - delta)};
         current_value <= group_max; ++current_value) {
      print_and_check(current_value);
    }
  }
  // special values are checked separately here
  BOOST_TEST_MESSAGE("Special iteration ");
  print_and_check_single(ValueType{});
  print_and_check(std::numeric_limits<ValueType>::max());
  if constexpr (std::is_signed_v<ValueType>) {
    print_and_check_single(std::numeric_limits<ValueType>::min());
  }
}
