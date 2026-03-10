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

#ifndef BINSRV_COMPOSITE_BINLOG_NAME_HPP
#define BINSRV_COMPOSITE_BINLOG_NAME_HPP

#include "binsrv/composite_binlog_name_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <string>
#include <string_view>

namespace binsrv {

class [[nodiscard]] composite_binlog_name {
public:
  static constexpr char components_separator{'.'};
  static constexpr std::size_t number_of_sequence_number_characters{6U};

  composite_binlog_name() = default;
  composite_binlog_name(std::string_view base_name,
                        std::uint32_t sequence_number);
  [[nodiscard]] static composite_binlog_name
  parse(std::string_view composite_name);

  [[nodiscard]] bool is_empty() const noexcept {
    return sequence_number_ == 0U;
  }

  [[nodiscard]] const std::string &get_base_name() const noexcept {
    return base_name_;
  }
  [[nodiscard]] std::uint32_t get_sequence_number() const noexcept {
    return sequence_number_;
  }

  [[nodiscard]] std::string str() const;

  composite_binlog_name next() const {
    return composite_binlog_name{get_base_name(), get_sequence_number() + 1U};
  }

  friend bool operator==(const composite_binlog_name &first,
                         const composite_binlog_name &second) = default;

private:
  std::string base_name_{};
  std::uint32_t sequence_number_{0U};
};

} // namespace binsrv

#endif // BINSRV_COMPOSITE_BINLOG_NAME_HPP
