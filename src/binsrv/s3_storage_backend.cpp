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
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/url/host_type.hpp>
#include <boost/url/scheme.hpp>
#include <boost/url/url_view_base.hpp>

#include <aws/core/Aws.h>

// TODO: remove this include when switching to clang-18 where transitive
//       IWYU 'export' pragmas are handled properly
#include "binsrv/basic_storage_backend_fwd.hpp"

#include "util/byte_span.hpp"
#include "util/exception_location_helpers.hpp"
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

s3_storage_backend::s3_storage_backend(const boost::urls::url_view_base &uri)
    : access_key_id_{}, secret_access_key_{}, root_path_{},
      options_{new Aws::SDKOptions} {
  Aws::InitAPI(*options_deimpl::get(options_));

  // "s3://<access_key_id>:<secret_access_key>@<bucket_name>/<path>" for AWS S3
  if (uri.scheme_id() != boost::urls::scheme::unknown ||
      uri.scheme() != uri_schema) {
    util::exception_location().raise<std::invalid_argument>(
        "URI of invalid scheme provided");
  }
  if (uri.host_type() != boost::urls::host_type::name || uri.host().empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "s3 URI must have host");
  }
  if (uri.host().find('.') != std::string::npos) {
    util::exception_location().raise<std::invalid_argument>(
        "s3 URI host must be a single bucket name");
  }
  bucket_ = uri.host();

  if (uri.has_port()) {
    util::exception_location().raise<std::invalid_argument>(
        "s3 URI must not have port");
  }

  if (uri.has_userinfo()) {
    if (!uri.has_password()) {
      util::exception_location().raise<std::invalid_argument>(
          "s3 URI must have either both user and password or none");
    }
    access_key_id_ = uri.user();
    secret_access_key_ = uri.password();
  }
  if (uri.has_query()) {
    util::exception_location().raise<std::invalid_argument>(
        "s3 URI must not have query");
  }
  if (uri.has_fragment()) {
    util::exception_location().raise<std::invalid_argument>(
        "s3 URI must not have fragment");
  }

  root_path_ = uri.path();
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
  res += ") - ";

  res += "bucket: ";
  res += bucket_;
  res += ", path: ";
  res += root_path_.generic_string();
  res += ", credentials: ";
  res += (has_credentials() ? "***hidden***" : "none");

  return res;
}
} // namespace binsrv
