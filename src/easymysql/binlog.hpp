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

#ifndef EASYMYSQL_BINLOG_HPP
#define EASYMYSQL_BINLOG_HPP

#include "easymysql/binlog_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "easymysql/connection_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace easymysql {

class [[nodiscard]] binlog {
  friend class connection;

public:
  binlog() = default;

  binlog(const binlog &) = delete;
  binlog(binlog &&) = default;
  binlog &operator=(const binlog &) = delete;
  binlog &operator=(binlog &&) = default;

  ~binlog();

  [[nodiscard]] bool is_empty() const noexcept { return !impl_; }

  // returns empty span on EOF
  // throws an exception on error
  util::const_byte_span fetch();

private:
  binlog(connection &conn, std::uint32_t server_id, std::string_view file_name,
         std::uint64_t position);

  connection *conn_{nullptr};
  struct rpl_deleter {
    void operator()(void *ptr) const noexcept;
  };
  using impl_ptr = std::unique_ptr<void, rpl_deleter>;
  impl_ptr impl_;
};

} // namespace easymysql

#endif // EASYMYSQL_BINLOG_HPP
