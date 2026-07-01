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

#include <cstdint>
#include <string_view>

#include <boost/asio/ts/netfwd.hpp>

namespace minimysql {

class network_service {
public:
  static constexpr auto expected_packet_size{4096UZ};
  static constexpr std::chrono::seconds session_authentication_timeout{10};
  static constexpr std::chrono::seconds session_command_timeout{120};

  network_service(boost::asio::io_context &context,
                  std::uint16_t listening_port, std::string_view username,
                  std::string_view password);

  network_service(const network_service &) = delete;
  network_service &operator=(const network_service &) = delete;
  network_service(network_service &&) = delete;
  network_service &operator=(network_service &&) = delete;

  ~network_service();

private:
  std::string username_;
  std::string password_;

  boost::asio::io_context *context_;
  using acceptor_type =
      boost::asio::basic_socket_acceptor<boost::asio::ip::tcp>;
  using acceptor_ptr = std::unique_ptr<acceptor_type>;
  acceptor_ptr acceptor_;
};

} // namespace minimysql

#endif // MINIMYSQL_NETWORK_SERVICE_HPP
