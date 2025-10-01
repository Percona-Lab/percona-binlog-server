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

#ifndef UTIL_SEMANTIC_VERSION_HPP
#define UTIL_SEMANTIC_VERSION_HPP

#include "util/semantic_version_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <string>
#include <string_view>

namespace util {

class [[nodiscard]] semantic_version {
private:
  static constexpr char component_delimiter{'.'};
  static constexpr char extra_delimiter{'-'};

  static constexpr std::uint32_t minor_multiplier{100U};
  static constexpr std::uint32_t major_multiplier{minor_multiplier *
                                                  minor_multiplier};

public:
  constexpr explicit semantic_version(
      std::uint32_t encoded_server_version) noexcept
      : major_{static_cast<std::uint8_t>(encoded_server_version /
                                         major_multiplier % minor_multiplier)},
        minor_{static_cast<std::uint8_t>(encoded_server_version /
                                         minor_multiplier % minor_multiplier)},
        patch_{static_cast<std::uint8_t>(encoded_server_version %
                                         minor_multiplier)} {}

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  constexpr semantic_version(std::uint8_t major, std::uint8_t minor,
                             std::uint8_t patch) noexcept
      : major_{major}, minor_{minor}, patch_{patch} {}
  explicit semantic_version(std::string_view version_string);

  [[nodiscard]] constexpr std::uint8_t get_major() const noexcept {
    return major_;
  }
  [[nodiscard]] constexpr std::uint8_t get_minor() const noexcept {
    return minor_;
  }
  [[nodiscard]] constexpr std::uint8_t get_patch() const noexcept {
    return patch_;
  }

  [[nodiscard]] constexpr std::uint32_t get_encoded() const noexcept {
    return (major_ * major_multiplier) + (minor_ * minor_multiplier) + patch_;
  }

  [[nodiscard]] std::string get_string() const {
    std::string result{};
    result += std::to_string(major_);
    result += component_delimiter;
    result += std::to_string(minor_);
    result += component_delimiter;
    result += std::to_string(patch_);
    return result;
  }

  [[nodiscard]] friend constexpr auto
  operator<=>(const semantic_version &first,
              const semantic_version &second) = default;

private:
  std::uint8_t major_;
  std::uint8_t minor_;
  std::uint8_t patch_;
};

} // namespace util

#endif // UTIL_SEMANTIC_VERSION_HPP
