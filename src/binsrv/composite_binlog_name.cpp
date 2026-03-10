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

#include "binsrv/composite_binlog_name.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <boost/lexical_cast.hpp>

#include <boost/math/special_functions/pow.hpp>

#include "util/exception_location_helpers.hpp"

namespace binsrv {

composite_binlog_name::composite_binlog_name(std::string_view base_name,
                                             std::uint32_t sequence_number)
    : base_name_{base_name}, sequence_number_{sequence_number} {
  if (base_name_.empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "binlog base name cannot be empty");
  }
  // currently checking only that the name does not include a filesystem
  // separator
  if (base_name_.find(std::filesystem::path::preferred_separator) !=
      std::string::npos) {
    util::exception_location().raise<std::invalid_argument>(
        "binlog base name contains filesystem separator");
  }
  if (sequence_number_ == 0U) {
    util::exception_location().raise<std::invalid_argument>(
        "binlog sequence number cannot be zero");
  }
  static constexpr std::uint32_t decimal_base{10U};
  if (sequence_number_ >=
      boost::math::pow<number_of_sequence_number_characters>(decimal_base)) {
    util::exception_location().raise<std::invalid_argument>(
        "binlog sequence number is too large");
  }
}

[[nodiscard]] composite_binlog_name
composite_binlog_name::parse(std::string_view composite_name) {
  if (std::size(composite_name) <= number_of_sequence_number_characters + 1U) {
    util::exception_location().raise<std::invalid_argument>(
        "binlog composite name is too short");
  }
  const auto separator_position{std::size(composite_name) -
                                number_of_sequence_number_characters - 1U};
  if (composite_name[separator_position] != components_separator) {
    util::exception_location().raise<std::invalid_argument>(
        "binlog composite name does not contain a base name / sequence number "
        "separator at expected position");
  }

  const auto sequence_number_sv{composite_name.substr(separator_position + 1U)};

  std::uint32_t sequence_number{};
  const auto [ptr, parse_ec]{std::from_chars(std::begin(sequence_number_sv),
                                             std::end(sequence_number_sv),
                                             sequence_number)};
  if (parse_ec != std::errc{} || ptr != std::end(sequence_number_sv)) {
    util::exception_location().raise<std::invalid_argument>(
        "binlog composite name contains invalid characters in its sequence "
        "number part");
  }

  const auto base_name_sv{composite_name.substr(0U, separator_position)};
  return composite_binlog_name{base_name_sv, sequence_number};
}

[[nodiscard]] std::string composite_binlog_name::str() const {
  return boost::lexical_cast<std::string>(*this);
}

std::ostream &operator<<(std::ostream &output,
                         const composite_binlog_name &obj) {
  static constexpr char sequence_number_filler{'0'};
  return output
         << obj.get_base_name() << composite_binlog_name::components_separator
         << std::setfill(sequence_number_filler)
         << std::setw(
                composite_binlog_name::number_of_sequence_number_characters)
         << obj.get_sequence_number();
}

} // namespace binsrv
