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

#ifndef BINSRV_S3_STORAGE_BACKEND_HPP
#define BINSRV_S3_STORAGE_BACKEND_HPP

#include <filesystem>
#include <string_view>

#include <boost/url/url_view_base.hpp>

#include "binsrv/basic_storage_backend.hpp" // IWYU pragma: export

namespace binsrv {

class [[nodiscard]] s3_storage_backend : public basic_storage_backend {
public:
  static constexpr std::string_view uri_schema{"s3"};

  explicit s3_storage_backend(const boost::urls::url_view_base &uri);

  [[nodiscard]] const std::string &get_bucket() const noexcept {
    return bucket_;
  }
  [[nodiscard]] const std::string &get_access_key_id() const noexcept {
    return access_key_id_;
  }
  [[nodiscard]] const std::string &get_secret_access_key() const noexcept {
    return secret_access_key_;
  }
  [[nodiscard]] bool has_credentials() const noexcept {
    return !access_key_id_.empty();
  }

  [[nodiscard]] const std::filesystem::path &get_root_path() const noexcept {
    return root_path_;
  }

private:
  std::string access_key_id_;
  // TODO: do not store secret_access_key in plain
  std::string secret_access_key_;
  std::string bucket_;
  std::filesystem::path root_path_;
  struct options_deleter {
    void operator()(void *ptr) const noexcept;
  };
  using options_ptr = std::unique_ptr<void, options_deleter>;
  options_ptr options_;

  [[nodiscard]] storage_object_name_container do_list_objects() override;

  [[nodiscard]] std::string do_get_object(std::string_view name) override;
  void do_put_object(std::string_view name,
                     util::const_byte_span content) override;

  void do_open_stream(std::string_view name) override;
  void do_write_data_to_stream(util::const_byte_span data) override;
  void do_close_stream() override;

  [[nodiscard]] std::string do_get_description() const override;
};

} // namespace binsrv

#endif // BINSRV_S3_STORAGE_BACKEND_HPP
