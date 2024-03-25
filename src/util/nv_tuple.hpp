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

#ifndef UTIL_NV_TUPLE_HPP
#define UTIL_NV_TUPLE_HPP

#include "util/nv_tuple_fwd.hpp" // IWYU pragma: export

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "util/ct_string.hpp"

namespace util {

template <ct_string CTS, typename T> struct nv {
  using type = T;
  static constexpr decltype(CTS) name{CTS};

  type value;
};

template <named_value... NVPack> struct nv_tuple : NVPack... {
  // TODO: add constraint to make sure that we have only uniqie names in the
  //       NVPack
  using this_type = nv_tuple<NVPack...>;

  static constexpr std::size_t size = sizeof...(NVPack);

  // clang-format off
  static constexpr std::size_t flattened_size{(
    []<typename T>(T) constexpr -> std::size_t {
      if constexpr (is_derived_from_named_value_tuple_v<typename T::type>) {
        return T::type::flattened_size;
      } else {
        return 1U;
      }
    }(std::type_identity<typename NVPack::type>{})
    + ... + 0U
  )};
  // clang-format on

  // clang-format off
  static constexpr std::size_t depth{std::max({
    []<typename T>(T) constexpr -> std::size_t {
        if constexpr (is_derived_from_named_value_tuple_v<typename T::type>) {
          return 1U + T::type::depth;
        } else {
          return 1U;
        }
      }(std::type_identity<typename NVPack::type>{})...
  })};
  // clang-format on

  template <std::size_t Index>
  using index_to_base = std::tuple_element_t<Index, std::tuple<NVPack...>>;

  template <std::size_t Index> [[nodiscard]] const auto &get() const noexcept {
    static_assert(Index < size, "nv_tuple index is out of range");
    return index_to_base<Index>::value;
  }
  template <std::size_t Index> [[nodiscard]] auto &get() noexcept {
    static_assert(Index < size, "nv_tuple index is out of range");
    return index_to_base<Index>::value;
  }

  // clang-format off
  template <ct_string CTS>
  static constexpr std::size_t name_to_index_v{
    // we use immediately invoked lambda here to calculate the index
    // of the element in the NVPack whose name is equal to CTS
    []<std::size_t... IndexPack>(std::index_sequence<IndexPack...>)
      -> std::size_t {
      // here we use a fold expression to calculate the sum of all the indexes
      // that correspond to an element with name equal to CTS
      // pseudo code:
      // std::size_t res{0U};
      // for constexpr (std::size_t i{0U}; i < sizeof...(NVPack); ++i)
      //   if constexpr (index_to_base<i>::name == CTS) res += (i + 1U);
      // return res == 0U ? size : res - 1U;
      const std::size_t res = (
        (index_to_base<IndexPack>::name == CTS ? IndexPack + 1U : 0U)
        + ... + 0U
      );
      return res == 0U ? size : res - 1U;
    }(std::index_sequence_for<NVPack...>{})};
  // clang-format on

  template <ct_string CTS> [[nodiscard]] const auto &get() const noexcept {
    static constexpr std::size_t found_index{name_to_index_v<CTS>};
    static_assert(found_index < size, "unknown nv_tuple element name");
    return get<found_index>();
  }
  template <ct_string CTS> [[nodiscard]] auto &get() noexcept {
    static constexpr std::size_t found_index{name_to_index_v<CTS>};
    static_assert(found_index < size, "unknown nv_tuple element name");
    return get<found_index>();
  }
};

} // namespace util

#endif // UTIL_NV_TUPLE_HPP
