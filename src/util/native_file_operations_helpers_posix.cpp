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

#include "util/native_file_operations_helpers.hpp"

#include <cerrno>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>

#include <boost/scope/scope_exit.hpp>

#include "util/exception_location_helpers.hpp"

namespace util {

void fsync(const std::filesystem::path &path) {
  // O_RDONLY is sufficient for 'fsync(2)' on both a regular file
  // and a directory's entry list; the kernel does
  // not require write access to flush.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int file_descriptor = ::open(path.c_str(), O_RDONLY);
  if (file_descriptor < 0) {
    const auto saved_errno = errno;
    exception_location().raise<std::runtime_error>(
        "cannot open path for fsync: " +
        std::error_code{saved_errno, std::generic_category()}.message());
  }

  const boost::scope::scope_exit close_guard{
      [&file_descriptor]() noexcept { ::close(file_descriptor); }};
  if (::fsync(file_descriptor) != 0) {
    const auto saved_errno = errno;
    exception_location().raise<std::runtime_error>(
        "cannot fsync path: " +
        std::error_code{saved_errno, std::generic_category()}.message());
  }
}

} // namespace util
