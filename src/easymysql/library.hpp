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

#ifndef EASYMYSQL_LIBRARY_HPP
#define EASYMYSQL_LIBRARY_HPP

#include "easymysql/library_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <string_view>

#include "easymysql/connection_config_fwd.hpp"
#include "easymysql/connection_fwd.hpp"

namespace easymysql {

class [[nodiscard]] library {
public:
  library();
  library(const library &) = delete;
  library(library &&) = delete;
  library &operator=(const library &) = delete;
  library &operator=(library &&) = delete;

  ~library();

  [[nodiscard]] std::uint32_t get_client_version() const noexcept;
  [[nodiscard]] std::string_view get_readable_client_version() const noexcept;

  [[nodiscard]] connection
  create_connection(const connection_config &config) const;
};

} // namespace easymysql

#endif // EASYMYSQL_LIBRARY_HPP
