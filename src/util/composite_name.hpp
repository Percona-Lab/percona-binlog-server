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

#ifndef UTIL_COMPOSIT_NAME_HPP
#define UTIL_COMPOSIT_NAME_HPP

#include "util/composite_name_fwd.hpp" // IWYU pragma: export

#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

namespace util {

template <typename StringLikeType> class [[nodiscard]] composite_name {
public:
  using element_type = StringLikeType;
  static constexpr char delimiter{'.'};

  composite_name() : elements_{}, total_length_{0U} {}
  explicit composite_name(const element_type &element)
      : elements_{1U, element}, total_length_{std::size(element)} {}

  void reserve(std::size_t number_of_elements) {
    elements_.reserve(number_of_elements);
  }

  [[nodiscard]] bool is_empty() const noexcept { return elements_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept {
    return std::size(elements_);
  }

  [[nodiscard]] const element_type &
  operator[](std::size_t index) const noexcept {
    assert(index < size());
    return elements_[index];
  }

  void push_back(const element_type &element) {
    elements_.push_back(element);
    if (size() > 1U) {
      ++total_length_;
    }
    total_length_ += std::size(element);
  }

  void pop_back() {
    assert(!is_empty());
    const std::size_t delta = std::size((*this)[size() - 1U]);
    elements_.pop_back();
    total_length_ -= delta;
    if (!is_empty()) {
      --total_length_;
    }
  }

  [[nodiscard]] std::string str() const {
    return str_internal(total_length_, std::cbegin(elements_),
                        std::cend(elements_));
  }
  [[nodiscard]] std::string str_reverse() const {
    return str_internal(total_length_, std::crbegin(elements_),
                        std::crend(elements_));
  }

private:
  using element_container = std::vector<element_type>;
  element_container elements_;
  std::size_t total_length_;

  template <typename Iterator>
  [[nodiscard]] static std::string str_internal(std::size_t total_length,
                                                Iterator begin, Iterator end) {
    std::string res;
    if (begin == end) {
      return res;
    }
    res.reserve(total_length);
    res.append(*begin);
    ++begin;
    while (begin != end) {
      res += delimiter;
      res.append(*begin);
      ++begin;
    }
    return res;
  }
};

} // namespace util

#endif // UTIL_COMPOSIT_NAME_HPP
