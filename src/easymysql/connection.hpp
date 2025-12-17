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

#include "easymysql/connection_config_fwd.hpp"
#include "easymysql/library_fwd.hpp"
#include "easymysql/ssl_config_fwd.hpp"
#include "easymysql/tls_config_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace easymysql {

class [[nodiscard]] connection {
  friend class library;
  friend struct raise_access;

public:
  connection() noexcept;

  connection(const connection &) = delete;
  connection(connection &&other) noexcept;
  connection &operator=(const connection &) = delete;
  connection &operator=(connection &&other) noexcept;

  ~connection();

  void swap(connection &other) noexcept;

  [[nodiscard]] bool is_empty() const noexcept { return !mysql_impl_; }

  [[nodiscard]] std::uint32_t get_server_version() const noexcept;
  [[nodiscard]] std::string_view get_readable_server_version() const noexcept;

  [[nodiscard]] std::uint32_t get_protocol_version() const noexcept;

  [[nodiscard]] std::string_view get_server_connection_info() const noexcept;
  [[nodiscard]] std::string_view get_character_set_name() const noexcept;

  void execute_generic_query_noresult(std::string_view query);
  [[nodiscard]] bool ping();

  [[nodiscard]] bool is_in_replication_mode() const noexcept {
    return static_cast<bool>(rpl_impl_);
  }

  void switch_to_position_replication(
      std::uint32_t server_id, std::string_view file_name,
      std::uint64_t position, bool verify_checksum,
      connection_replication_mode_type blocking_mode);
  // a simplified version for starting from the very beginning
  void switch_to_position_replication(
      std::uint32_t server_id, bool verify_checksum,
      connection_replication_mode_type blocking_mode);

  void switch_to_gtid_replication(
      std::uint32_t server_id, util::const_byte_span encoded_gtid_set,
      bool verify_checksum, connection_replication_mode_type blocking_mode);

  // returns false on 'connection closed' / 'timeout'
  // returns true and sets 'portion' to en empty span on EOF (last event read)
  // returns true and sets 'portion' to event data on success
  // throws an exception on any error other than 'connection closed' / 'timeout'
  [[nodiscard]] bool fetch_binlog_event(util::const_byte_span &portion);

private:
  void set_binlog_checksum(bool verify_checksum);

  void process_ssl_config(const ssl_config &config);
  void process_tls_config(const tls_config &config);
  void process_connection_config(const connection_config &config);

  explicit connection(const connection_config &config);

  struct mysql_deleter {
    void operator()(void *ptr) const noexcept;
  };
  using mysql_impl_ptr = std::unique_ptr<void, mysql_deleter>;
  mysql_impl_ptr mysql_impl_;

  class rpl_impl;
  using rpl_impl_ptr = std::unique_ptr<rpl_impl>;
  rpl_impl_ptr rpl_impl_;
};

} // namespace easymysql

#endif // EASYMYSQL_CONNECTION_HPP
