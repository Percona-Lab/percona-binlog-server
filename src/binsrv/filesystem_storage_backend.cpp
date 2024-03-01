#include "binsrv/filesystem_storage_backend.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

// TODO: remove this include when switching to clang-18 where transitive
//       IWYU 'export' pragmas are handled properly
#include "binsrv/basic_storage_backend_fwd.hpp"

#include "util/byte_span.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

filesystem_storage_backend::filesystem_storage_backend(
    std::string_view root_path)
    : root_path_{root_path}, ofs_{} {
  // TODO: switch to utf8 file names
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
  const auto index_path{get_object_path(name)};

  static constexpr std::size_t max_file_size{1048576U};

  // opening in binary mode
  std::ifstream ifs{index_path, std::ios_base::binary};
  if (!ifs.is_open()) {
    util::exception_location().raise<std::runtime_error>(
        "cannot open undellying object file");
  }
  auto file_size = std::filesystem::file_size(index_path);
  if (file_size > max_file_size) {
    util::exception_location().raise<std::out_of_range>(
        "undellying object file is too large");
  }

  std::string file_content(file_size, 'x');
  if (!ifs.read(std::data(file_content),
                static_cast<std::streamoff>(file_size))) {
    util::exception_location().raise<std::runtime_error>(
        "cannot read undellying object file content");
  }
  return file_content;
}

void filesystem_storage_backend::do_put_object(std::string_view name,
                                               util::const_byte_span content) {
  const auto index_path = get_object_path(name);
  // opening in binary mode with truncating
  std::ofstream index_ofs{index_path, std::ios_base::trunc};
  if (!index_ofs.is_open()) {
    util::exception_location().raise<std::runtime_error>(
        "cannot open undellying object file for writing");
  }
  const auto content_sv = util::as_string_view(content);
  if (!index_ofs.write(std::data(content_sv),
                       static_cast<std::streamoff>(std::size(content_sv)))) {
    util::exception_location().raise<std::runtime_error>(
        "cannot write date to undellying object file");
  }
}

void filesystem_storage_backend::do_open_stream(std::string_view name) {
  std::filesystem::path current_file_path{root_path_};
  current_file_path /= name;

  ofs_.open(current_file_path, std::ios_base::binary | std::ios_base::app);
  if (!ofs_.is_open()) {
    util::exception_location().raise<std::runtime_error>(
        "cannot open underlying file for the stream");
  }
}

void filesystem_storage_backend::do_write_data_to_stream(
    util::const_byte_span data) {
  const auto data_sv = util::as_string_view(data);
  if (!ofs_.write(std::data(data_sv),
                  static_cast<std::streamoff>(std::size(data_sv)))) {
    util::exception_location().raise<std::runtime_error>(
        "cannot write data to the underlying stream file");
  }
  // TODO: make sure that the data is properly written to the disk
  //       use fsync() system call here
}

void filesystem_storage_backend::do_close_stream() { ofs_.close(); }

[[nodiscard]] std::filesystem::path
filesystem_storage_backend::get_object_path(std::string_view name) const {
  auto result{root_path_};
  result /= name;
  return result;
}

} // namespace binsrv
