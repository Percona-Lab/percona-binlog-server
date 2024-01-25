#include "binsrv/filesystem_storage.hpp"

#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string_view>

#include "binsrv/event/protocol_traits_fwd.hpp"

#include "util/byte_span.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

filesystem_storage::filesystem_storage(std::string_view root_path)
    : root_path_{root_path}, binlog_names_{}, ofs_{} {
  if (!std::filesystem::exists(root_path_)) {
    util::exception_location().raise<std::invalid_argument>(
        "root path does not exist");
  }
  if (!std::filesystem::is_directory(root_path_)) {
    util::exception_location().raise<std::invalid_argument>(
        "root path is not a directory");
  }

  // TODO: convert this into reading directory content and extracting
  //       binlog file name / position
  if (!std::filesystem::is_empty(root_path_)) {
    util::exception_location().raise<std::invalid_argument>(
        "root path directory is not empty");
  }
}

[[nodiscard]] bool
filesystem_storage::check_binlog_name(std::string_view binlog_name) noexcept {
  // TODO: parse binlog name into "base name" and "rotation number"
  //       e.g. "binlog.000001" -> ("binlog", 1)

  // currently checking only that the name does not include a filesystem
  // separator
  return binlog_name.find(std::filesystem::path::preferred_separator) ==
         std::string_view::npos;
}

void filesystem_storage::open_binlog(std::string_view binlog_name) {
  if (ofs_.is_open()) {
    util::exception_location().raise<std::logic_error>(
        "cannot create a binlog as the previous one has noot been closed");
  }
  if (!check_binlog_name(binlog_name)) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot create a binlog with invalid file name");
  }

  std::filesystem::path current_file_path{root_path_};
  current_file_path /= binlog_name;
  // opening in binary mode with truncation
  ofs_.open(current_file_path, std::ios_base::binary | std::ios_base::trunc);
  if (!ofs_.is_open()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot create a binlog file");
  }
  if (!ofs_.write(std::data(event::magic_binlog_payload),
                  static_cast<std::streamoff>(
                      std::size(event::magic_binlog_payload)))) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot write magic payload to the binlog");
  }

  binlog_names_.emplace_back(binlog_name);
  append_to_binlog_index(binlog_name);
}

void filesystem_storage::write_event(util::const_byte_span event_data) {
  // TODO: make sure that the data is properly written to the disk
  //       use fsync() system call here

  if (!ofs_.is_open()) {
    util::exception_location().raise<std::logic_error>(
        "cannot write to the binlog file as it has not been opened");
  }
  const auto event_data_sv = util::as_string_view(event_data);
  if (!ofs_.write(std::data(event_data_sv),
                  static_cast<std::streamoff>(std::size(event_data_sv)))) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot write event data to the binlog");
  }
  // TODO: consider adding fsync() call here
}
void filesystem_storage::close_binlog() {
  if (!ofs_.is_open()) {
    util::exception_location().raise<std::logic_error>(
        "cannot close the binlog file as it has not been opened");
  }
  ofs_.close();
}

void filesystem_storage::append_to_binlog_index(std::string_view binlog_name) {
  std::filesystem::path index_path{root_path_};
  index_path /= default_binlog_index_name;
  // opening in text mode with appending
  std::ofstream index_ofs{index_path, std::ios_base::app};
  if (!index_ofs.is_open()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot open binlog index file");
  }
  std::filesystem::path binlog_path{"./"};
  binlog_path /= binlog_name;

  index_ofs << binlog_path.generic_string() << '\n';
  if (!index_ofs) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot append to the binlog index file");
  }
}

} // namespace binsrv
