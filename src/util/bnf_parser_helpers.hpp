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

#ifndef UTIL_BNF_PARSER_HELPERS_HPP
#define UTIL_BNF_PARSER_HELPERS_HPP

#include <stdexcept>
#include <string_view>

namespace util {

[[nodiscard]] inline bool check_character(std::string_view &remainder,
                                          char expected_character) noexcept {
  if (remainder.empty()) {
    return false;
  }
  return remainder.front() == expected_character;
}

[[nodiscard]] inline bool parse_character_ex(std::string_view &remainder,
                                             char expected_character,
                                             char &result) noexcept {
  if (!check_character(remainder, expected_character)) {
    return false;
  }
  result = expected_character;
  remainder.remove_prefix(1U);
  return true;
}

inline char parse_character(std::string_view &remainder,
                            char expected_character) {
  char result{};
  if (!parse_character_ex(remainder, expected_character, result)) {
    throw std::invalid_argument{"character does not match the expected one"};
  }
  return result;
}

template <typename Predicate>
[[nodiscard]] inline bool
check_character_predicate(std::string_view &remainder,
                          Predicate predicate) noexcept {
  if (remainder.empty()) {
    return false;
  }
  return predicate(remainder.front());
}

template <typename Predicate>
[[nodiscard]] inline bool
parse_character_predicate_ex(std::string_view &remainder, Predicate predicate,
                             char &result) noexcept {
  if (!check_character_predicate(remainder, predicate)) {
    return false;
  }
  result = remainder.front();
  remainder.remove_prefix(1U);
  return true;
}

template <typename Predicate>
inline char parse_character_predicate(std::string_view &remainder,
                                      Predicate predicate) {
  char result{};
  if (!parse_character_predicate_ex(remainder, predicate, result)) {
    throw std::invalid_argument{
        "character does not satisfy the specified predicate"};
  }
  return result;
}

} // namespace util

#endif // UTIL_BNF_PARSER_HELPERS_HPP
