// Copyright (c) 2026 Percona and/or its affiliates.
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

#include "binsrv/instance_config.hpp"

#include <filesystem>
#include <stdexcept>

#include "util/exception_location_helpers.hpp"

namespace binsrv {

void instance_config::validate() const {
  const auto &local_settings_path{get<"local_settings_path">()};
  if (local_settings_path.empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating instance config: "
        "local_settings_path must not be empty");
  }
  // 'exists' / 'is_directory' both follow symlinks, so a symlink that
  // targets a directory is accepted and a dangling symlink is rejected.
  const std::filesystem::path path{local_settings_path};
  if (!std::filesystem::exists(path)) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating instance config: "
        "local_settings_path does not exist");
  }
  if (!std::filesystem::is_directory(path)) {
    util::exception_location().raise<std::invalid_argument>(
        "error validating instance config: "
        "local_settings_path must be a directory");
  }
}

} // namespace binsrv
