#ifndef UTIL_NV_TUPLE_HPP
#define UTIL_NV_TUPLE_HPP

#include <cstddef>
#include <tuple>
#include <utility>

#include "util/ct_string.hpp"

namespace util {

// most probably due to a bug in the bugprone-exception-escape
// clanng-tidy rule logic, the exception that may potentially come from the
// special functions of both nv and nv_tuple class templates is not properly
// handled

// NOLINTNEXTLINE(bugprone-exception-escape)
template <ct_string CTS, typename T> struct nv {
  using type = T;
  static constexpr decltype(CTS) name = CTS;

  type value;
};

// NOLINTNEXTLINE(bugprone-exception-escape)
template <typename... NVPack> struct nv_tuple : NVPack... {
  // TODO: add constraint to make sure that all NVPack elements are of type nv
  // TODO: add constraint to make sure that we have only uniqie names in the
  //       NVPack
  static constexpr std::size_t size = sizeof...(NVPack);

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

  template <ct_string CTS>
  static constexpr std::size_t name_to_index_v =
      // we use immediately invoked lambda here to calculate the index
      // of the element in the NVPack whose name is equal to CTS
      []<std::size_t... IndexPack>(std::index_sequence<IndexPack...>) {
    // here we use a fold expression to calculatethe sum of all the indexes
    // that correspond to an element with name equal to CTS
    // pseudo code:
    // std::size_t res = 0;
    // for constexpr (std::size_t i = 0; i < sizeof...(NVPack); ++i)
    //   if constexpr (index_to_base<i>::name == CTS) res += (i + 1);
    // return res == 0 ? size : res - 1;
    auto res = ((index_to_base<IndexPack>::name == CTS ? IndexPack + 1U : 0U) +
                ... + 0U);
    return res == 0 ? size : res - 1;
  }
  (std::index_sequence_for<NVPack...>{});

  template <ct_string CTS> [[nodiscard]] const auto &get() const noexcept {
    static constexpr std::size_t found_index = name_to_index_v<CTS>;
    static_assert(found_index < size, "unknown nv_tuple element name");
    return get<found_index>();
  }
  template <ct_string CTS> [[nodiscard]] auto &get() noexcept {
    static constexpr std::size_t found_index = name_to_index_v<CTS>;
    static_assert(found_index < size, "unknown nv_tuple element name");
    return get<found_index>();
  }
};

} // namespace util

#endif // UTIL_NV_TUPLE_HPP
