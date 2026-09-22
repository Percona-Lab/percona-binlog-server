// Copyright (c) 2023-2026 Percona and/or its affiliates.
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

#ifndef MINIMYSQL_NETWORK_SERVICE_HPP
#define MINIMYSQL_NETWORK_SERVICE_HPP

#include <boost/asio/ts/netfwd.hpp>

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/replication_source_config_fwd.hpp"
#include "binsrv/storage_fwd.hpp"

namespace minimysql {

class network_service {
public:
  static constexpr auto expected_packet_size{4096UZ};

  network_service(binsrv::basic_logger_ptr logger,
                  boost::asio::io_context &context, binsrv::storage_ptr storage,
                  const binsrv::replication_source_config &cfg);

  network_service(const network_service &) = delete;
  network_service &operator=(const network_service &) = delete;
  network_service(network_service &&) = delete;
  network_service &operator=(network_service &&) = delete;

  ~network_service();

private:
  binsrv::basic_logger_ptr logger_;
  binsrv::storage_ptr storage_;

  boost::asio::io_context *context_;
  using acceptor_type =
      boost::asio::basic_socket_acceptor<boost::asio::ip::tcp>;
  using acceptor_ptr = std::unique_ptr<acceptor_type>;
  acceptor_ptr acceptor_;
};

} // namespace minimysql

#endif // MINIMYSQL_NETWORK_SERVICE_HPP
