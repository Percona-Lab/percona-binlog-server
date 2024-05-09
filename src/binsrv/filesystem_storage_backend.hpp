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

#ifndef BINSRV_FILESYSTEM_STORAGE_BACKEND_HPP
#define BINSRV_FILESYSTEM_STORAGE_BACKEND_HPP

#include <filesystem>
#include <fstream>
#include <string_view>

#include <boost/url/url_view_base.hpp>

#include "binsrv/basic_storage_backend.hpp" // IWYU pragma: export

namespace binsrv {

class [[nodiscard]] filesystem_storage_backend final
    : public basic_storage_backend {
public:
  static constexpr std::size_t max_memory_object_size{1048576U};

  static constexpr std::string_view uri_schema{"file"};

  explicit filesystem_storage_backend(const boost::urls::url_view_base &uri);

  [[nodiscard]] const std::filesystem::path &get_root_path() const noexcept {
    return root_path_;
  }

private:
  std::filesystem::path root_path_;
  std::ofstream ofs_;

  [[nodiscard]] storage_object_name_container do_list_objects() override;

  [[nodiscard]] std::string do_get_object(std::string_view name) override;
  void do_put_object(std::string_view name,
                     util::const_byte_span content) override;

  void do_open_stream(std::string_view name,
                      storage_backend_open_stream_mode mode) override;
  void do_write_data_to_stream(util::const_byte_span data) override;
  void do_close_stream() override;

  [[nodiscard]] std::string do_get_description() const override;

  [[nodiscard]] std::filesystem::path
  get_object_path(std::string_view name) const;
};

} // namespace binsrv

#endif // BINSRV_FILESYSTEM_STORAGE_BACKEND_HPP
