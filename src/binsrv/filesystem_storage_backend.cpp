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

#include "binsrv/filesystem_storage_backend.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/url/host_type.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/scheme.hpp>

#include "binsrv/storage_config.hpp"

#include "util/byte_span.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

filesystem_storage_backend::filesystem_storage_backend(
    const storage_config &config)
    : root_path_{}, ofs_{} {
  // TODO: switch to utf8 file names

  const auto &backend_uri = config.get<"uri">();

  const auto uri_parse_result{boost::urls::parse_absolute_uri(backend_uri)};
  if (!uri_parse_result) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid storage backend URI");
  }
  const auto &uri{*uri_parse_result};

  // "file://<path>" for local filesystem
  if (uri.scheme_id() != boost::urls::scheme::file ||
      uri.scheme() != uri_schema) {
    util::exception_location().raise<std::invalid_argument>(
        "URI of invalid scheme provided");
  }
  if (uri.host_type() != boost::urls::host_type::name || !uri.host().empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "file URI must not have host");
  }
  if (uri.has_port()) {
    util::exception_location().raise<std::invalid_argument>(
        "file URI must not have port");
  }
  if (uri.has_userinfo()) {
    util::exception_location().raise<std::invalid_argument>(
        "file URI must not have userinfo");
  }
  if (uri.has_query()) {
    util::exception_location().raise<std::invalid_argument>(
        "file URI must not have query");
  }
  if (uri.has_fragment()) {
    util::exception_location().raise<std::invalid_argument>(
        "file URI must not have fragment");
  }

  root_path_ = uri.path();

  if (!std::filesystem::exists(root_path_)) {
    util::exception_location().raise<std::invalid_argument>(
        "root path does not exist");
  }
  if (!std::filesystem::is_directory(root_path_)) {
    util::exception_location().raise<std::invalid_argument>(
        "root path is not a directory");
  }
}

[[nodiscard]] storage_object_name_container
filesystem_storage_backend::do_list_objects() {
  storage_object_name_container result;
  for (auto const &dir_entry :
       std::filesystem::directory_iterator{root_path_}) {
    if (!dir_entry.is_regular_file()) {
      util::exception_location().raise<std::logic_error>(
          "filesystem storage directory contains an entry that is not a "
          "regular file");
    }
    // TODO: check permissions here
    result.emplace(dir_entry.path().filename().string(), dir_entry.file_size());
  }
  return result;
}

[[nodiscard]] std::string
filesystem_storage_backend::do_get_object(std::string_view name) {
  const auto object_path{get_object_path(name)};

  // opening in binary mode
  std::ifstream object_ifs{object_path,
                           std::ios_base::in | std::ios_base::binary};
  if (!object_ifs.is_open()) {
    util::exception_location().raise<std::runtime_error>(
        "cannot open undellying object file");
  }
  auto file_size = std::filesystem::file_size(object_path);
  if (file_size > max_memory_object_size) {
    util::exception_location().raise<std::out_of_range>(
        "undellying object file is too large to be loaded in memory");
  }

  std::string file_content(file_size, 'x');
  if (!object_ifs.read(std::data(file_content),
                       static_cast<std::streamoff>(file_size))) {
    util::exception_location().raise<std::runtime_error>(
        "cannot read undellying object file content");
  }
  return file_content;
}

void filesystem_storage_backend::do_put_object(std::string_view name,
                                               util::const_byte_span content) {
  const auto object_path = get_object_path(name);
  // opening in binary mode with truncating
  std::ofstream object_ofs{object_path, std::ios_base::out |
                                            std::ios_base::binary |
                                            std::ios_base::trunc};
  if (!object_ofs.is_open()) {
    util::exception_location().raise<std::runtime_error>(
        "cannot open undellying object file for writing");
  }
  const auto content_sv = util::as_string_view(content);
  if (!object_ofs.write(std::data(content_sv),
                        static_cast<std::streamoff>(std::size(content_sv)))) {
    util::exception_location().raise<std::runtime_error>(
        "cannot write date to undellying object file");
  }
}

void filesystem_storage_backend::do_open_stream(
    std::string_view name, storage_backend_open_stream_mode mode) {
  assert(!ofs_.is_open());
  const std::filesystem::path current_file_path{get_object_path(name)};

  auto open_mode{std::ios_base::out | std::ios_base::binary |
                 (mode == storage_backend_open_stream_mode::create
                      ? std::ios_base::trunc
                      : std::ios_base::app)};
  ofs_.open(current_file_path, open_mode);
  if (!ofs_.is_open()) {
    util::exception_location().raise<std::runtime_error>(
        "cannot open underlying file for the stream");
  }
}

void filesystem_storage_backend::do_write_data_to_stream(
    util::const_byte_span data) {
  assert(ofs_.is_open());
  const auto data_sv = util::as_string_view(data);
  if (!ofs_.write(std::data(data_sv),
                  static_cast<std::streamoff>(std::size(data_sv)))) {
    util::exception_location().raise<std::runtime_error>(
        "cannot write data to the underlying stream file");
  }
  if (!ofs_.flush()) {
    util::exception_location().raise<std::runtime_error>(
        "cannot flush the underlying stream file");
  }
  // TODO: make sure that the data is properly written to the disk
  //       use fsync() system call here
}

void filesystem_storage_backend::do_close_stream() {
  assert(ofs_.is_open());
  ofs_.close();
}

[[nodiscard]] std::string
filesystem_storage_backend::do_get_description() const {
  return "local filesystem - " + root_path_.generic_string();
}

[[nodiscard]] std::filesystem::path
filesystem_storage_backend::get_object_path(std::string_view name) const {
  auto result{root_path_};
  result /= name;
  return result;
}

} // namespace binsrv
