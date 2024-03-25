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

#include "binsrv/storage.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "binsrv/basic_storage_backend.hpp"

#include "binsrv/event/protocol_traits_fwd.hpp"

#include "util/byte_span.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

storage::storage(basic_storage_backend_ptr backend)
    : backend_{std::move(backend)}, binlog_names_{} {
  const auto storage_objects{backend_->list_objects()};
  if (storage_objects.empty()) {
    // initialized on a new / empty storage - no other actions required
    return;
  }

  if (!storage_objects.contains(default_binlog_index_name)) {
    util::exception_location().raise<std::logic_error>(
        "storage is not empty but does not contain binlog index");
  }

  load_binlog_index();
  validate_binlog_index(storage_objects);

  if (!binlog_names_.empty()) {
    // call to validate_binlog_index() guarantees that the name will be
    // found here
    position_ = storage_objects.at(binlog_names_.back());
  }
}

[[nodiscard]] bool
storage::check_binlog_name(std::string_view binlog_name) noexcept {
  // TODO: parse binlog name into "base name" and "rotation number"
  //       e.g. "binlog.000001" -> ("binlog", 1)

  // currently checking only that the name does not include a filesystem
  // separator
  return binlog_name.find(std::filesystem::path::preferred_separator) ==
         std::string_view::npos;
}

void storage::open_binlog(std::string_view binlog_name) {
  if (!check_binlog_name(binlog_name)) {
    util::exception_location().raise<std::logic_error>(
        "cannot create a binlog with invalid name");
  }

  backend_->open_stream(binlog_name);

  if (position_ == 0ULL) {
    // writing the magic binlog footprint only if this is a newly
    // created file
    backend_->write_data_to_stream(event::magic_binlog_payload);

    binlog_names_.emplace_back(binlog_name);
    save_binlog_index();
    position_ = event::magic_binlog_offset;
  }
}

void storage::write_event(util::const_byte_span event_data) {
  backend_->write_data_to_stream(event_data);
  position_ += std::size(event_data);
}

void storage::close_binlog() {
  backend_->close_stream();
  position_ = 0ULL;
}

void storage::load_binlog_index() {
  const auto index_content{backend_->get_object(default_binlog_index_name)};
  // opening in text mode
  std::istringstream index_iss{index_content};
  std::string current_line;
  while (std::getline(index_iss, current_line)) {
    if (current_line.empty()) {
      continue;
    }
    const std::filesystem::path current_binlog_path{current_line};
    if (current_binlog_path.parent_path() != default_binlog_index_entry_path) {
      util::exception_location().raise<std::logic_error>(
          "binlog index contains an entry that has an invalid path");
    }
    auto current_binlog_name{current_binlog_path.filename().string()};

    if (current_binlog_name == default_binlog_index_name) {
      util::exception_location().raise<std::logic_error>(
          "binlog index contains a reference to the binlog index name");
    }
    if (!check_binlog_name(current_binlog_name)) {
      util::exception_location().raise<std::logic_error>(
          "binlog index contains a reference to a binlog with invalid "
          "name");
    }
    if (std::ranges::find(binlog_names_, current_binlog_name) !=
        std::end(binlog_names_)) {
      util::exception_location().raise<std::logic_error>(
          "binlog index contains a duplicate entry");
    }
    binlog_names_.emplace_back(std::move(current_binlog_name));
  }
}

void storage::validate_binlog_index(
    const storage_object_name_container &object_names) {
  std::size_t known_entries{0U};
  for (auto const &[object_name, object_size] : object_names) {
    if (object_name == default_binlog_index_name) {
      continue;
    }
    if (std::ranges::find(binlog_names_, object_name) ==
        std::end(binlog_names_)) {
      util::exception_location().raise<std::logic_error>(
          "storage contains an object that is not "
          "referenced in the binlog index");
    }
    ++known_entries;
  }

  if (known_entries != std::size(binlog_names_)) {
    util::exception_location().raise<std::logic_error>(
        "binlog index contains a reference to a non-existing object");
  }

  // TODO: add integrity checks (parsing + checksumming) for the binlog
  //       files in the index
}

void storage::save_binlog_index() {
  std::ostringstream oss;
  for (const auto &binlog_name : binlog_names_) {
    std::filesystem::path binlog_path{default_binlog_index_entry_path};
    binlog_path /= binlog_name;
    oss << binlog_path.generic_string() << '\n';
  }
  const auto content{oss.str()};
  backend_->put_object(default_binlog_index_name,
                       util::as_const_byte_span(content));
}

} // namespace binsrv
