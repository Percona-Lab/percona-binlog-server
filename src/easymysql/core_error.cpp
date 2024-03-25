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

#include "easymysql/core_error.hpp"

#include <string>
#include <system_error>

namespace easymysql {

const std::error_category &mysql_category() noexcept {
  class [[nodiscard]] category_impl : public std::error_category {
  public:
    [[nodiscard]] const char *name() const noexcept override {
      return "mysql_client";
    }
    [[nodiscard]] std::string message(int code) const override {
      // We cannot use ER_CLIENT() function here as
      // it will return message strings with printf '%' parameters
      return "CR_" + std::to_string(code);
    }
  };

  static const category_impl instance;
  return instance;
}

core_error::core_error(int native_error_code)
    : std::system_error{make_mysql_error_code(native_error_code)} {}
core_error::core_error(int native_error_code, const std::string &what)
    : std::system_error{make_mysql_error_code(native_error_code), what} {}
core_error::core_error(int native_error_code, const char *what)
    : std::system_error{make_mysql_error_code(native_error_code), what} {}

} // namespace easymysql
