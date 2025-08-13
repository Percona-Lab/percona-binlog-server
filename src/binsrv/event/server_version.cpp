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

#include "binsrv/event/server_version.hpp"

#include <charconv>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

server_version::server_version(std::string_view version_string)
    : major_{}, minor_{}, patch_{} {
  const auto dash_pos{version_string.find('-')};
  if (dash_pos != std::string::npos) {
    version_string.remove_suffix(version_string.size() - dash_pos);
  }
  auto split_result{std::ranges::views::split(version_string, '.')};
  auto version_it{std::ranges::begin(split_result)};
  const auto version_en{std::ranges::end(split_result)};

  const auto component_extractor{
      [version_en](auto &cuttent_it, const std::string &name) -> std::uint8_t {
        if (cuttent_it == version_en) {
          util::exception_location().raise<std::invalid_argument>(
              "server_version does not have the \"" + name + "\" component");
        }
        std::uint8_t result{};
        const auto component_it{std::ranges::cbegin(*cuttent_it)};
        const auto component_en{std::ranges::cend(*cuttent_it)};
        const auto [conversion_ptr, conversion_ec]{
            std::from_chars(component_it, component_en, result)};
        if (conversion_ec != std::errc{} || conversion_ptr != component_en) {
          util::exception_location().raise<std::invalid_argument>(
              "server_version \"" + name +
              "\" component is not a numeric value");
        }
        ++cuttent_it;
        return result;
      }};
  major_ = component_extractor(version_it, "major");
  minor_ = component_extractor(version_it, "minor");
  patch_ = component_extractor(version_it, "patch");
  if (version_it != version_en) {
    util::exception_location().raise<std::invalid_argument>(
        "server_version has more than 3 components");
  }
}

} // namespace binsrv::event
