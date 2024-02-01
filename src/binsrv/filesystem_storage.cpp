#include "binsrv/filesystem_storage.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "binsrv/event/protocol_traits_fwd.hpp"

#include "util/byte_span.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

filesystem_storage::filesystem_storage(std::string_view root_path)
    : root_path_{root_path}, binlog_names_{}, ofs_{} {
  // TODO: switch to utf8 file names
  if (!std::filesystem::exists(root_path_)) {
    util::exception_location().raise<std::invalid_argument>(
        "root path does not exist");
  }
  if (!std::filesystem::is_directory(root_path_)) {
    util::exception_location().raise<std::invalid_argument>(
        "root path is not a directory");
  }

  const auto index_path = get_index_path();
  if (!std::filesystem::exists(index_path)) {
    if (!std::filesystem::is_empty(root_path_)) {
      util::exception_location().raise<std::invalid_argument>(
          "root path directory does not contain the binlog index file and is "
          "not empty");
    }
  } else {
    load_binlog_index(index_path);
  }
  if (!binlog_names_.empty()) {
    position_ = std::filesystem::file_size(root_path_ / binlog_names_.back());
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

  const bool is_appending{position_ != 0ULL};
  // opening in binary mode with appending either appending or truncating
  // depending on whether the storage was initialized on a non-empty
  // directory or not
  const auto mode{std::ios_base::binary |
                  (is_appending ? std::ios_base::app : std::ios_base::trunc)};
  ofs_.open(current_file_path, mode);
  if (!ofs_.is_open()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot create a binlog file");
  }
  // writing the magic binlog footprint only if this is a newly
  // created file
  if (!is_appending) {
    if (!ofs_.write(std::data(event::magic_binlog_payload),
                    static_cast<std::streamoff>(
                        std::size(event::magic_binlog_payload)))) {
      util::exception_location().raise<std::invalid_argument>(
          "cannot write magic payload to the binlog");
    }
  }

  if (!is_appending) {
    binlog_names_.emplace_back(binlog_name);
    append_to_binlog_index(binlog_name);
    position_ = event::magic_binlog_offset;
  }
}

void filesystem_storage::write_event(util::const_byte_span event_data) {
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
  position_ += std::size(event_data);
  // TODO: make sure that the data is properly written to the disk
  //       use fsync() system call here
}
void filesystem_storage::close_binlog() {
  if (!ofs_.is_open()) {
    util::exception_location().raise<std::logic_error>(
        "cannot close the binlog file as it has not been opened");
  }
  ofs_.close();
  position_ = 0ULL;
}

[[nodiscard]] std::filesystem::path filesystem_storage::get_index_path() const {
  auto result{root_path_};
  result /= default_binlog_index_name;
  return result;
}

void filesystem_storage::load_binlog_index(
    const std::filesystem::path &index_path) {
  if (!std::filesystem::is_regular_file(index_path)) {
    util::exception_location().raise<std::invalid_argument>(
        "the binlog index file is not a regular file");
  }
  // opening in text mode
  std::ifstream index_ifs{index_path};
  if (!index_ifs.is_open()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot open the binlog index file for reading");
  }
  std::string current_line;
  while (std::getline(index_ifs, current_line)) {
    if (current_line.empty()) {
      continue;
    }
    const std::filesystem::path current_binlog_path{current_line};
    if (current_binlog_path.parent_path() != default_binlog_index_entry_path) {
      util::exception_location().raise<std::invalid_argument>(
          "binlog index contains an entry that has an invalid path");
    }
    auto current_binlog_name{current_binlog_path.filename().string()};

    if (current_binlog_name == default_binlog_index_name) {
      util::exception_location().raise<std::invalid_argument>(
          "binlog index contains a reference to the binlog index file name");
    }
    if (!check_binlog_name(current_binlog_name)) {
      util::exception_location().raise<std::invalid_argument>(
          "binlog index contains a reference a binlog file with invalid "
          "name");
    }
    if (std::ranges::find(binlog_names_, current_binlog_name) !=
        std::end(binlog_names_)) {
      util::exception_location().raise<std::invalid_argument>(
          "binlog index contains a duplicate entry");
    }
    binlog_names_.emplace_back(std::move(current_binlog_name));
  }

  std::size_t known_entries{0U};
  for (auto const &dir_entry :
       std::filesystem::directory_iterator{root_path_}) {
    if (!dir_entry.is_regular_file()) {
      util::exception_location().raise<std::invalid_argument>(
          "filesystem storage directory contains an entry that is not a "
          "regular file");
    }
    // TODO: check permissions here
    const auto &dir_entry_path = dir_entry.path();
    const auto dir_entry_filename = dir_entry_path.filename().string();

    if (dir_entry_filename == default_binlog_index_name) {
      continue;
    }
    if (std::ranges::find(binlog_names_, dir_entry_filename) ==
        std::end(binlog_names_)) {
      util::exception_location().raise<std::invalid_argument>(
          "filesystem storage directory contains an entry that is not "
          "referenced in the binlog index file");
    }
    ++known_entries;
  }

  if (known_entries != binlog_names_.size()) {
    util::exception_location().raise<std::invalid_argument>(
        "binlog index contains a reference to a non-existing file");
  }

  // TODO: add integrity checks (parsing + checksumming) for the binlog
  //       files in the index
}

void filesystem_storage::append_to_binlog_index(std::string_view binlog_name) {
  const auto index_path = get_index_path();
  // opening in text mode with appending
  std::ofstream index_ofs{index_path, std::ios_base::app};
  if (!index_ofs.is_open()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot open binlog index file for updating");
  }
  std::filesystem::path binlog_path{default_binlog_index_entry_path};
  binlog_path /= binlog_name;

  index_ofs << binlog_path.generic_string() << '\n';
  if (!index_ofs) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot append to the binlog index file");
  }
}

} // namespace binsrv
