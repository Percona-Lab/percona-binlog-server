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

#include "binsrv/s3_error_helpers_private.hpp"

#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>

#include <aws/s3-crt/S3CrtErrors.h>

#include "binsrv/s3_error.hpp"

#include "util/exception_location_helpers.hpp"

namespace binsrv {

[[noreturn]] void
raise_s3_error_from_outcome(std::string_view user_message,
                            const Aws::S3Crt::S3CrtError &sdk_error,
                            std::source_location location) {
  std::string message{};
  if (!user_message.empty()) {
    message += user_message;
    message += ": ";
  }

  message += '<';
  message += sdk_error.GetExceptionName();
  message += "> - ";
  message += sdk_error.GetMessage();
  message += " (IP ";
  message += sdk_error.GetRemoteHostIpAddress();
  message += ", RequestID ";
  message += sdk_error.GetRequestId();
  message += ", ResponseCode ";
  // TODO: in c++23 change to std::to_underlying()
  auto http_status_code = sdk_error.GetResponseCode();
  message += std::to_string(
      static_cast<std::underlying_type_t<decltype(http_status_code)>>(
          http_status_code));
  message += ')';

  // default value std::source_location::current() for the 'location'
  // parameter is specified in the declaration of this function
  // and passed to the util::exception_location's constructor
  // instead of calling util::exception_location's default constructor
  // because otherwise the location will always point to this
  // particular line on this particular file regardless of the actual
  // location from where this function is called
  util::exception_location(location).raise<s3_error>(
      static_cast<int>(sdk_error.GetErrorType()), message);
}

} // namespace binsrv
