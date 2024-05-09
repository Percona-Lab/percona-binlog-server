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

#include "binsrv/s3_error.hpp"

#include <string>
#include <system_error>

namespace binsrv {

const std::error_category &s3_category() noexcept {
  class [[nodiscard]] category_impl : public std::error_category {
  public:
    [[nodiscard]] const char *name() const noexcept override {
      return "aws_s3_crt";
    }
    [[nodiscard]] std::string message(int code) const override {
      return "AWS_S3_CRT_" + std::to_string(code);
    }
  };

  static const category_impl instance;
  return instance;
}

s3_error::s3_error(int native_error_code)
    : std::system_error{make_s3_error_code(native_error_code)} {}
s3_error::s3_error(int native_error_code, const std::string &what)
    : std::system_error{make_s3_error_code(native_error_code), what} {}
s3_error::s3_error(int native_error_code, const char *what)
    : std::system_error{make_s3_error_code(native_error_code), what} {}

} // namespace binsrv
