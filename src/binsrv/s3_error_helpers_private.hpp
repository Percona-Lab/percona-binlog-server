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

#ifndef BINSRV_S3_ERROR_HELPERS_PRIVATE_HPP
#define BINSRV_S3_ERROR_HELPERS_PRIVATE_HPP

#include <source_location>
#include <string_view>

#include <aws/s3-crt/S3CrtErrors.h>

namespace binsrv {

[[noreturn]] void raise_s3_error_from_outcome(
    std::string_view user_message, const Aws::S3Crt::S3CrtError &sdk_error,
    std::source_location location = std::source_location::current());

} // namespace binsrv

#endif // BINSRV_S3_ERROR_HELPERS_PRIVATE_HPP
