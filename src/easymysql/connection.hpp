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

#ifndef EASYMYSQL_CONNECTION_HPP
#define EASYMYSQL_CONNECTION_HPP

#include "easymysql/connection_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <memory>
#include <string_view>

#include "easymysql/binlog_fwd.hpp"
#include "easymysql/connection_config_fwd.hpp"
#include "easymysql/library_fwd.hpp"

namespace easymysql {

class [[nodiscard]] connection {
  friend class library;
  friend class binlog;
  friend struct raise_access;

public:
  connection() = default;

  connection(const connection &) = delete;
  connection(connection &&) = default;
  connection &operator=(const connection &) = delete;
  connection &operator=(connection &&) = default;

  ~connection() = default;

  [[nodiscard]] bool is_empty() const noexcept { return !impl_; }

  [[nodiscard]] std::uint32_t get_server_version() const noexcept;
  [[nodiscard]] std::string_view get_readable_server_version() const noexcept;

  [[nodiscard]] std::uint32_t get_protocol_version() const noexcept;

  [[nodiscard]] std::string_view get_server_connection_info() const noexcept;
  [[nodiscard]] std::string_view get_character_set_name() const noexcept;

  [[nodiscard]] binlog create_binlog(std::uint32_t server_id,
                                     std::string_view file_name,
                                     std::uint64_t position);
  void execute_generic_query_noresult(std::string_view query);

private:
  explicit connection(const connection_config &config);

  struct connection_deleter {
    void operator()(void *ptr) const noexcept;
  };
  using impl_ptr = std::unique_ptr<void, connection_deleter>;
  impl_ptr impl_;
};

} // namespace easymysql

#endif // EASYMYSQL_CONNECTION_HPP
