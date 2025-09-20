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

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <istream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <boost/url/host_type.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/scheme.hpp>
#include <boost/url/url_view_base.hpp>

#include <boost/uuid/uuid_io.hpp>

#include <aws/core/Aws.h>

#include <aws/core/auth/AWSCredentials.h>

#include <aws/core/http/Scheme.h>

#include <aws/core/utils/memory/AWSMemory.h>

#include <aws/s3-crt/ClientConfiguration.h>
#include <aws/s3-crt/S3CrtClient.h>

#include <aws/s3-crt/model/BucketLocationConstraint.h>
#include <aws/s3-crt/model/GetBucketLocationRequest.h>
#include <aws/s3-crt/model/GetBucketLocationResult.h>
#include <aws/s3-crt/model/GetObjectRequest.h>
#include <aws/s3-crt/model/GetObjectResult.h>
#include <aws/s3-crt/model/ListObjectsV2Request.h>
#include <aws/s3-crt/model/ListObjectsV2Result.h>
#include <aws/s3-crt/model/PutObjectRequest.h>

#include "binsrv/s3_error_helpers_private.hpp"
#include "binsrv/storage_config.hpp"

#include "util/byte_span.hpp"
#include "util/exception_location_helpers.hpp"

namespace {

class aws_context_base {
public:
  aws_context_base() { Aws::InitAPI(options_); }
  aws_context_base(const aws_context_base &) = delete;
  aws_context_base &operator=(const aws_context_base &) = delete;
  aws_context_base(aws_context_base &&) = delete;
  aws_context_base &operator=(aws_context_base &&) = delete;
  ~aws_context_base() { Aws::ShutdownAPI(options_); }

  [[nodiscard]] const Aws::SDKOptions &get_options() const noexcept {
    return options_;
  }

private:
  Aws::SDKOptions options_;
};

} // namespace

namespace binsrv {

class simple_aws_credentials {
public:
  explicit simple_aws_credentials(const boost::urls::url_view_base &uri)
      : access_key_id_{uri.user()}, secret_access_key_{uri.password()} {}

  [[nodiscard]] const std::string &get_access_key_id() const noexcept {
    return access_key_id_;
  }
  [[nodiscard]] const std::string &get_secret_access_key() const noexcept {
    return secret_access_key_;
  }

private:
  std::string access_key_id_;
  std::string secret_access_key_;
};

struct qualified_object_path {
  std::string bucket;
  std::filesystem::path object_path;
};

class s3_storage_backend::aws_context : private aws_context_base {
public:
  struct bucket_tag {};
  struct region_tag {};
  struct endpoint_tag {};

  using stream_factory_type = std::function<std::iostream *()>;
  using stream_handler_type = std::function<void(std::size_t, std::iostream &)>;

  aws_context(bucket_tag tag, const simple_aws_credentials &credentials,
              const std::string &bucket);
  aws_context(region_tag tag, const simple_aws_credentials &credentials,
              const std::string &region);
  aws_context(endpoint_tag tag, const simple_aws_credentials &credentials,
              const std::string &endpoint, bool secure_protocol);

  aws_context(const aws_context &) = delete;
  aws_context &operator=(const aws_context &) = delete;
  aws_context(aws_context &&) = delete;
  aws_context &operator=(aws_context &&) = delete;
  ~aws_context() = default;

  using aws_context_base::get_options;
  [[nodiscard]] bool has_credentials() const noexcept {
    return !credentials_.IsEmpty();
  }

  [[nodiscard]] bool has_endpoint() const noexcept {
    return !configuration_.endpointOverride.empty();
  }

  [[nodiscard]] const std::string &get_endpoint() const noexcept {
    return configuration_.endpointOverride;
  }

  [[nodiscard]] const std::string &get_region() const noexcept {
    return configuration_.region;
  }

  [[nodiscard]] std::string_view get_scheme_label() const noexcept {
    return Aws::Http::SchemeMapper::ToString(configuration_.scheme);
  }

  [[nodiscard]] std::string get_bucket_region(const std::string &bucket) const;

  [[nodiscard]] std::string
  get_object_into_string(const qualified_object_path &source) const;

  void
  get_object_into_file(const qualified_object_path &source,
                       const std::filesystem::path &content_file_path) const;

  void put_object_from_stream(const qualified_object_path &dest,
                              std::iostream &content_stream) const;

  void put_object_from_span(const qualified_object_path &dest,
                            util::const_byte_span content) const;

  [[nodiscard]] storage_object_name_container
  list_objects(const qualified_object_path &prefix);

private:
  using iostream_ptr = std::shared_ptr<std::iostream>;
  // TODO: do not store secret_access_key in plain
  Aws::Auth::AWSCredentials credentials_;
  Aws::S3Crt::ClientConfiguration configuration_;
  using s3_crt_client_ptr = std::unique_ptr<Aws::S3Crt::S3CrtClient>;
  s3_crt_client_ptr client_;

  explicit aws_context(const simple_aws_credentials &credentials)
      : aws_context_base{}, credentials_{credentials.get_access_key_id(),
                                         credentials.get_secret_access_key()},
        configuration_{} {}

  void get_object_internal(const qualified_object_path &source,
                           const stream_factory_type &stream_factory,
                           const stream_handler_type &stream_handler) const;
};

s3_storage_backend::aws_context::aws_context(
    bucket_tag /*tag*/, const simple_aws_credentials &credentials,
    const std::string &bucket)
    : aws_context{credentials} {
  // if the construction_alternative is 'bucket', leave the 'region' field in
  // the configuration class in its default state ("us-east-1") and try to
  // detect AWS S3 region from the bucket location
  client_ =
      std::make_unique<Aws::S3Crt::S3CrtClient>(credentials_, configuration_);

  configuration_.region = get_bucket_region(bucket);
  // reset to nullptr first to make sure we do not have two clients
  // simultaneously
  client_.reset();
  client_ =
      std::make_unique<Aws::S3Crt::S3CrtClient>(credentials_, configuration_);
}

s3_storage_backend::aws_context::aws_context(
    region_tag /*tag*/, const simple_aws_credentials &credentials,
    const std::string &region)
    : aws_context{credentials} {
  // if the provided construction_alternative is 'region', initialize S3 client
  // with the provided region parameter
  configuration_.region = region;
  client_ =
      std::make_unique<Aws::S3Crt::S3CrtClient>(credentials_, configuration_);
}

s3_storage_backend::aws_context::aws_context(
    endpoint_tag /*tag*/, const simple_aws_credentials &credentials,
    const std::string &endpoint, bool secure_protocol)
    : aws_context{credentials} {
  // if the provided construction_alternative is 'endpoint', initialize S3
  // client with the provided endpoint parameter

  // configuration_.region should hold the default value which is "us-east-1"
  configuration_.scheme =
      (secure_protocol ? Aws::Http::Scheme::HTTPS : Aws::Http::Scheme::HTTP);
  // In case of alternative S3 endpoints, we always use legacy URL composing
  // scheme (where bucket name is not a part of the host but the very first
  // segment of the path).
  configuration_.useVirtualAddressing = false;
  configuration_.endpointOverride = endpoint;
  client_ =
      std::make_unique<Aws::S3Crt::S3CrtClient>(credentials_, configuration_);
}

[[nodiscard]] std::string s3_storage_backend::aws_context::get_bucket_region(
    const std::string &bucket) const {
  Aws::S3Crt::Model::GetBucketLocationRequest bucket_location_request{};
  bucket_location_request.SetBucket(bucket);
  const auto bucket_location_outcome{
      client_->GetBucketLocation(bucket_location_request)};
  if (!bucket_location_outcome.IsSuccess()) {
    raise_s3_error_from_outcome("cannot identify bucket location",
                                bucket_location_outcome.GetError());
  }
  const auto model_region =
      bucket_location_outcome.GetResult().GetLocationConstraint();
  return Aws::S3Crt::Model::BucketLocationConstraintMapper::
      GetNameForBucketLocationConstraint(model_region);
}

[[nodiscard]] std::string
s3_storage_backend::aws_context::get_object_into_string(
    const qualified_object_path &source) const {
  std::string content;
  auto stream_handler{[&content](std::size_t content_length,
                                 std::iostream &content_stream) {
    // TODO: check object length in advance before calling GetObject
    //       (with HeadObject, for instance)
    if (content_length > max_memory_object_size) {
      util::exception_location().raise<std::out_of_range>(
          "S3 object is too large to be loaded in memory");
    }

    content.resize(content_length);
    if (!content_stream.read(std::data(content),
                             static_cast<std::streamsize>(content_length))) {
      util::exception_location().raise<std::runtime_error>(
          "cannot read S3 object content into a string");
    }
    assert(content_stream.gcount() ==
           static_cast<std::streamsize>(content_length));
  }};
  get_object_internal(source, {}, stream_handler);

  return content;
}

void s3_storage_backend::aws_context::get_object_into_file(
    const qualified_object_path &source,
    const std::filesystem::path &content_file_path) const {
  auto stream_factory{[&content_file_path]() -> std::iostream * {
    return Aws::New<std::fstream>(
        "GetObjectStreamFactoryAllocationTag", content_file_path,
        std::ios_base::in | std::ios_base::out | std::ios_base::binary |
            std::ios_base::trunc);
  }};
  std::size_t response_content_length{};
  auto stream_handler{
      [&response_content_length](std::size_t content_length,
                                 std::iostream &content_stream) {
        content_stream.seekg(0, std::ios_base::end);

        const auto end_position{
            static_cast<std::streamoff>(content_stream.tellg())};
        if (!std::in_range<std::size_t>(end_position) ||
            static_cast<std::size_t>(end_position) != content_length) {
          util::exception_location().raise<std::runtime_error>(
              "cannot read S3 object content into a file");
        }
        response_content_length = content_length;
      }};

  get_object_internal(source, stream_factory, stream_handler);
  assert(std::filesystem::file_size(content_file_path) ==
         response_content_length);
}

void s3_storage_backend::aws_context::put_object_from_stream(
    const qualified_object_path &dest, std::iostream &content_stream) const {
  Aws::S3Crt::Model::PutObjectRequest put_object_request;
  put_object_request.SetBucket(dest.bucket);
  put_object_request.SetKey(dest.object_path.generic_string());

  // making an shared pointer with noop deleter using aliasing constructor
  const iostream_ptr wrapped_content_stream{iostream_ptr{}, &content_stream};

  put_object_request.SetBody(wrapped_content_stream);

  const auto put_object_outcome{client_->PutObject(put_object_request)};

  if (!put_object_outcome.IsSuccess()) {
    raise_s3_error_from_outcome("cannot put object into S3 bucket",
                                put_object_outcome.GetError());
  }
}

void s3_storage_backend::aws_context::put_object_from_span(
    const qualified_object_path &dest, util::const_byte_span content) const {
  std::string materialized_content{util::as_string_view(content)};
  std::stringstream content_stream{std::move(materialized_content),
                                   std::ios_base::in | std::ios_base::out |
                                       std::ios_base::binary};
  put_object_from_stream(dest, content_stream);
}

[[nodiscard]] storage_object_name_container
s3_storage_backend::aws_context::list_objects(
    const qualified_object_path &prefix) {
  storage_object_name_container result;

  Aws::S3Crt::Model::ListObjectsV2Request list_objects_request;
  list_objects_request.SetBucket(prefix.bucket);

  // for the list operation request the prefix must not start with "/""

  // moreover, in order to exclude false positives in situations when
  // both "/foo" and "/foobar" directories are present and we need to list
  // the content of the "/foo" directory, the prefix should include a
  // trailing slash "foo/"

  // for the "/" prefix path we should set the prefix to an empty string /
  // not set the prefix at all

  // relative path of a default-constructed object / "" / "/" is an empty
  // path
  auto prefix_path{prefix.object_path.relative_path()};

  // appending an empty path to an existing path will just add a trailing
  // separator, e.g. "foo" -> "foo/" (for en empty path this operation has
  // no effect)
  prefix_path /= std::filesystem::path{};

  const auto prefix_str{prefix_path.generic_string()};

  if (!prefix_str.empty()) {
    list_objects_request.SetPrefix(prefix_str);
  }

  const auto list_objects_outcome{client_->ListObjectsV2(list_objects_request)};
  if (!list_objects_outcome.IsSuccess()) {
    raise_s3_error_from_outcome("cannot list objects in the specified bucket",
                                list_objects_outcome.GetError());
  }
  const auto &list_objects_result = list_objects_outcome.GetResult();
  // TODO: implement receiving the rest of the list
  if (list_objects_result.GetIsTruncated()) {
    util::exception_location().raise<std::logic_error>(
        "too many objects in the specified bucket");
  }

  auto model_key_count = list_objects_result.GetKeyCount();
  if (!std::in_range<std::size_t>(model_key_count)) {
    util::exception_location().raise<std::logic_error>(
        "invalid key count in the list objects result");
  }
  auto key_count{static_cast<std::size_t>(model_key_count)};

  const auto &model_objects{list_objects_result.GetContents()};
  if (key_count != std::size(model_objects)) {
    util::exception_location().raise<std::logic_error>(
        "key count does not match the number of objects in the list objects "
        "result");
  }
  result.reserve(key_count);

  for (const auto &model_object : model_objects) {
    // if the prefix is set, the list of objects in the response will include
    // the prefix itself (as a directory) with zero size - it neeeds to be
    // skipped

    // moreover, we need to remove the prefix itself from the object paths
    std::string_view key{model_object.GetKey()};
    if (!prefix_str.empty()) {
      if (!key.starts_with(prefix_str)) {
        util::exception_location().raise<std::logic_error>(
            "encountered an object with unexpected prefix");
      }
      key.remove_prefix(prefix_str.size());
    }
    if (!key.empty()) {
      result.emplace(key, model_object.GetSize());
    }
  }

  return result;
}

void s3_storage_backend::aws_context::get_object_internal(
    const qualified_object_path &source,
    const stream_factory_type &stream_factory,
    const stream_handler_type &stream_handler) const {
  Aws::S3Crt::Model::GetObjectRequest get_object_request;
  if (stream_factory) {
    get_object_request.SetResponseStreamFactory(stream_factory);
  }
  get_object_request.SetBucket(source.bucket);
  get_object_request.SetKey(source.object_path.generic_string());

  const auto get_object_outcome{client_->GetObject(get_object_request)};

  if (!get_object_outcome.IsSuccess()) {
    raise_s3_error_from_outcome("cannot get object from S3 bucket",
                                get_object_outcome.GetError());
  }
  const auto &get_object_result{get_object_outcome.GetResult()};
  auto &content_stream{get_object_result.GetBody()};

  const auto model_content_length{get_object_result.GetContentLength()};
  if (!std::in_range<std::size_t>(model_content_length)) {
    util::exception_location().raise<std::logic_error>(
        "invalid S3 object content length");
  }

  const auto content_length{static_cast<std::size_t>(model_content_length)};

  if (stream_handler) {
    stream_handler(content_length, content_stream);
  }
}

s3_storage_backend::s3_storage_backend(const storage_config &config)
    : bucket_{}, root_path_{}, current_name_{}, uuid_generator_{},
      tmp_file_directory_{}, current_tmp_file_path_{}, tmp_fstream_{}, impl_{} {
  // TODO: take into account S3 limits (like 5GB single file upload)

  const auto &opt_fs_buffer_directory{config.get<"fs_buffer_directory">()};
  if (opt_fs_buffer_directory.has_value()) {
    tmp_file_directory_ = *opt_fs_buffer_directory;
  } else {
    tmp_file_directory_ = boost::uuids::to_string(uuid_generator_());
  }

  const auto tmp_file_directory_status{
      std::filesystem::status(tmp_file_directory_)};
  if (tmp_file_directory_status.type() ==
      std::filesystem::file_type::not_found) {
    std::error_code fs_error{};
    std::filesystem::create_directories(tmp_file_directory_, fs_error);
    if (fs_error) {
      util::exception_location().raise<std::invalid_argument>(
          "unable to create storage backend filesystem buffer directory \"" +
          tmp_file_directory_.string() + "\"");
    }
  } else if (tmp_file_directory_status.type() !=
             std::filesystem::file_type::directory) {
    util::exception_location().raise<std::invalid_argument>(
        "the specified storage backend filesystem buffer directory \"" +
        tmp_file_directory_.string() + "\" is not a directory");
  }

  const auto &backend_uri{config.get<"uri">()};

  const auto uri_parse_result{boost::urls::parse_absolute_uri(backend_uri)};
  if (!uri_parse_result) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid storage backend URI");
  }
  const auto &uri{*uri_parse_result};

  // "s3://[<access_key_id>:<secret_access_key>@]<bucket_name>[.<region>]/<path>"
  // for AWS S3
  // "http[s]://[<access_key_id>:<secret_access_key>@]<endpoint>[:<port>]/<bucket/><path>"
  // for S3-compatible storages

  if (uri.has_query()) {
    util::exception_location().raise<std::invalid_argument>(
        "s3 URI must not have query");
  }
  if (uri.has_fragment()) {
    util::exception_location().raise<std::invalid_argument>(
        "s3 URI must not have fragment");
  }
  if (uri.has_userinfo()) {
    if (!uri.has_password()) {
      util::exception_location().raise<std::invalid_argument>(
          "s3 URI must have either both user and password or none");
    }
  }

  switch (uri.scheme_id()) {
  case boost::urls::scheme::http:
  case boost::urls::scheme::https:
    init_with_endpoint(uri);
    break;
  case boost::urls::scheme::unknown:
    if (uri.scheme() != original_uri_schema) {
      util::exception_location().raise<std::invalid_argument>(
          "The only supported custom URI scheme is " +
          std::string{original_uri_schema});
    }
    init_with_bucket_or_region(uri);
    break;
  default:
    util::exception_location().raise<std::invalid_argument>(
        "URI of invalid scheme provided");
  }
}

s3_storage_backend::~s3_storage_backend() {
  if (!tmp_fstream_.is_open()) {
    return;
  }
  // bugprone-empty-catch should not be that strict in destructors
  try {
    close_stream_internal();
  } catch (...) { // NOLINT(bugprone-empty-catch)
  }
}

void s3_storage_backend::init_with_bucket_or_region(
    const boost::urls::url_view_base &uri) {
  // "s3://" case
  assert(uri.scheme_id() == boost::urls::scheme::unknown);
  assert(uri.scheme() == original_uri_schema);

  if (uri.host_type() != boost::urls::host_type::name || uri.host().empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "original s3 URI must have host");
  }
  if (uri.has_port()) {
    util::exception_location().raise<std::invalid_argument>(
        "original s3 URI must not have port");
  }

  static constexpr char host_delimiter{'.'};
  root_path_ = uri.path();
  bucket_ = uri.host();
  const auto host_fnd{bucket_.find(host_delimiter)};
  if (host_fnd == std::string::npos) {
    // "<bucket_name>" branch (without "<region>")
    impl_ = std::make_unique<aws_context>(aws_context::bucket_tag{},
                                          simple_aws_credentials{uri}, bucket_);
  } else {
    // "<bucket_name>.<region>" branch
    const std::string region(bucket_, host_fnd + 1);
    bucket_.resize(host_fnd);
    if (bucket_.empty()) {
      util::exception_location().raise<std::invalid_argument>(
          "original s3 URI bucket name must not be empty");
    }
    if (region.empty()) {
      util::exception_location().raise<std::invalid_argument>(
          "original s3 URI region must not be empty");
    }
    if (region.find(host_delimiter) != std::string::npos) {
      util::exception_location().raise<std::invalid_argument>(
          "original s3 URI region must not be a qualified name");
    }
    impl_ = std::make_unique<aws_context>(aws_context::region_tag{},
                                          simple_aws_credentials{uri}, region);
  }
}

void s3_storage_backend::init_with_endpoint(
    const boost::urls::url_view_base &uri) {
  // "http[s]://" case
  assert(uri.scheme_id() == boost::urls::scheme::http ||
         uri.scheme_id() == boost::urls::scheme::https);

  if (uri.path().empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "endpoint s3 URI path must not be empty");
  }

  // When URI scheme is either HTTP or HTTPS, the host and port
  // part of this URI are threated as an alternative S3-compatible
  // server endpoint.
  const bool secure_protocol{uri.scheme_id() == boost::urls::scheme::https};
  std::string endpoint{uri.host()};
  if (uri.has_port()) {
    static constexpr char port_delimiter{':'};
    endpoint += port_delimiter;
    endpoint += uri.port();
  }

  // Also, as in the case of an S3-compatible storage we use
  // legacy URL composing scheme (where bucket name is not a
  // part of the host name but the very first segment of the path).
  // As the result, in the provided storage URI configuration parameter we
  // consider its first path segment a buckedt name.
  const std::filesystem::path uri_path{uri.path()};
  auto uri_path_it{std::begin(uri_path)};
  assert(uri_path_it != std::end(uri_path));
  assert(*uri_path_it == "/");
  root_path_.assign(*uri_path_it);
  ++uri_path_it;

  if (uri_path_it == std::end(uri_path)) {
    util::exception_location().raise<std::invalid_argument>(
        "endpoint s3 URI path must include at least on segment");
  }
  bucket_ = *uri_path_it;
  ++uri_path_it;
  for (; uri_path_it != std::end(uri_path); ++uri_path_it) {
    root_path_ /= *uri_path_it;
  }

  impl_ = std::make_unique<aws_context>(aws_context::endpoint_tag{},
                                        simple_aws_credentials{uri}, endpoint,
                                        secure_protocol);
}

[[nodiscard]] storage_object_name_container
s3_storage_backend::do_list_objects() {
  return impl_->list_objects({.bucket = bucket_, .object_path = root_path_});
}

[[nodiscard]] std::string
s3_storage_backend::do_get_object(std::string_view name) {
  return impl_->get_object_into_string(
      {.bucket = bucket_, .object_path = get_object_path(name)});
}

void s3_storage_backend::do_put_object(std::string_view name,
                                       util::const_byte_span content) {
  impl_->put_object_from_span(
      {.bucket = bucket_, .object_path = get_object_path(name)}, content);
}

void s3_storage_backend::do_open_stream(std::string_view name,
                                        storage_backend_open_stream_mode mode) {
  assert(!tmp_fstream_.is_open());
  current_name_ = name;
  current_tmp_file_path_ = generate_tmp_file_path();

  if (mode == storage_backend_open_stream_mode::append) {
    impl_->get_object_into_file(
        {.bucket = bucket_, .object_path = get_object_path(current_name_)},
        current_tmp_file_path_);
  }

  tmp_fstream_.open(current_tmp_file_path_,
                    std::ios_base::in | std::ios_base::out |
                        std::ios_base::binary | std::ios_base::app);
  if (!tmp_fstream_.is_open()) {
    util::exception_location().raise<std::runtime_error>(
        "cannot open temporary file for S3 object body stream");
  }
}

void s3_storage_backend::do_write_data_to_stream(util::const_byte_span data) {
  assert(tmp_fstream_.is_open());
  const auto data_sv{util::as_string_view(data)};
  if (!tmp_fstream_.write(std::data(data_sv),
                          static_cast<std::streamoff>(std::size(data_sv)))) {
    util::exception_location().raise<std::runtime_error>(
        "cannot write data to the temporary file for S3 object");
  }
  if (!tmp_fstream_.flush()) {
    util::exception_location().raise<std::runtime_error>(
        "cannot flush the temporary file for S3 object");
  }
}

void s3_storage_backend::do_flush_stream() { upload_tmp_stream_internal(); }

void s3_storage_backend::do_close_stream() {
  assert(tmp_fstream_.is_open());
  close_stream_internal();
  current_tmp_file_path_.clear();
  current_name_.clear();
}

[[nodiscard]] std::string s3_storage_backend::do_get_description() const {
  std::string res{"AWS S3 (SDK "};
  const auto &options = impl_->get_options();
  res += std::to_string(options.sdkVersion.major);
  res += '.';
  res += std::to_string(options.sdkVersion.minor);
  res += '.';
  res += std::to_string(options.sdkVersion.patch);
  res += ") - ";

  if (impl_->has_endpoint()) {
    res += "endpoint: ";
    res += impl_->get_scheme_label();
    res += "://";
    res += impl_->get_endpoint();
  } else {
    res += "region: ";
    res += impl_->get_region();
  }
  res += ", bucket: ";
  res += bucket_;
  res += ", path: ";
  res += root_path_.generic_string();
  res += ", credentials: ";
  res += (impl_->has_credentials() ? "***hidden***" : "none");

  return res;
}

[[nodiscard]] std::filesystem::path
s3_storage_backend::get_object_path(std::string_view name) const {
  auto result{root_path_};
  result /= name;
  return result;
}

[[nodiscard]] std::filesystem::path
s3_storage_backend::generate_tmp_file_path() {
  return tmp_file_directory_ / boost::uuids::to_string(uuid_generator_());
}

void s3_storage_backend::upload_tmp_stream_internal() {
  // S3 object may already exist, it is OK to overwrite it here
  impl_->put_object_from_stream(
      {.bucket = bucket_, .object_path = get_object_path(current_name_)},
      tmp_fstream_);
}

void s3_storage_backend::close_stream_internal() {
  upload_tmp_stream_internal();
  tmp_fstream_.close();
  // we allow std::filesystem::remove() here to fail - worst case scenario
  // we will have a temorary file not removed
  std::error_code remove_ec;
  std::filesystem::remove(current_tmp_file_path_, remove_ec);
}

} // namespace binsrv
