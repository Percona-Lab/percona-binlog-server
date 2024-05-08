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

#ifndef BINSRV_S3_ERROR_HPP
#define BINSRV_S3_ERROR_HPP

#include <stdexcept>
#include <system_error>

namespace binsrv {

[[nodiscard]] const std::error_category &s3_category() noexcept;

[[nodiscard]] inline std::error_code
make_s3_error_code(int native_error_code) noexcept {
  return {native_error_code, s3_category()};
}

class [[nodiscard]] s3_error : public std::system_error {
public:
  explicit s3_error(int native_error_code);
  s3_error(int native_error_code, const std::string &what);
  s3_error(int native_error_code, const char *what);
};

} // namespace binsrv

#endif // BINSRV_S3_ERROR_HPP
