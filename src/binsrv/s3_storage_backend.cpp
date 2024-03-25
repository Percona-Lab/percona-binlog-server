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

#include "binsrv/s3_storage_backend.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <aws/core/Aws.h>

// TODO: remove this include when switching to clang-18 where transitive
//       IWYU 'export' pragmas are handled properly
#include "binsrv/basic_storage_backend_fwd.hpp"

#include "util/byte_span.hpp"
#include "util/impl_helpers.hpp"

namespace binsrv {

using options_deimpl = util::deimpl<Aws::SDKOptions>;

void s3_storage_backend::options_deleter::operator()(void *ptr) const noexcept {
  auto *casted_ptr{static_cast<Aws::SDKOptions *>(ptr)};
  Aws::ShutdownAPI(*casted_ptr);
  // deleting via std::default_delete to avoid
  // cppcoreguidelines-owning-memory warnings
  using delete_helper = std::default_delete<Aws::SDKOptions>;
  delete_helper{}(casted_ptr);
}

s3_storage_backend::s3_storage_backend(std::string_view root_path)
    : root_path_{root_path}, options_{new Aws::SDKOptions} {
  Aws::InitAPI(*options_deimpl::get(options_));
}

[[nodiscard]] storage_object_name_container
s3_storage_backend::do_list_objects() {
  storage_object_name_container result;
  return result;
}

[[nodiscard]] std::string
s3_storage_backend::do_get_object(std::string_view /*name*/) {
  static constexpr std::size_t file_size{8};
  std::string file_content(file_size, 'x');
  return file_content;
}

void s3_storage_backend::do_put_object(std::string_view /*name*/,
                                       util::const_byte_span /*content*/) {}

void s3_storage_backend::do_open_stream(std::string_view /*name*/) {}

void s3_storage_backend::do_write_data_to_stream(
    util::const_byte_span /*data*/) {}

void s3_storage_backend::do_close_stream() {}

[[nodiscard]] std::string s3_storage_backend::do_get_description() const {
  std::string res{"AWS S3 (SDK "};
  const auto &casted_options = *options_deimpl::get(options_);
  res += std::to_string(casted_options.sdkVersion.major);
  res += '.';
  res += std::to_string(casted_options.sdkVersion.minor);
  res += '.';
  res += std::to_string(casted_options.sdkVersion.patch);
  res += ')';
  return res;
}
} // namespace binsrv
