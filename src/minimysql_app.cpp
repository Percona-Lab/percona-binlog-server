// Copyright (c) 2023-20264 Percona and/or its affiliates.
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

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"

#include <boost/asio/io_context.hpp>

#pragma GCC diagnostic pop

#include <boost/asio/signal_set.hpp>

#include "minimysql/network_service.hpp"

int main(int /* argc */, char * /* argv */[]) {
  static constexpr std::uint16_t listening_port{3307};

  static constexpr std::string_view default_username{"rpl"};
  static constexpr std::string_view default_password{"password"};

  int res{EXIT_FAILURE};
  try {
    std::cout << "starting mini-mysql-server" << '\n';
    boost::asio::io_context ctx;
    const minimysql::network_service service(
        ctx, listening_port, default_username, default_password);

    boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { ctx.stop(); });

    const auto ctx_run_result{ctx.run()};
    std::cout << "ctx.run() returned " << ctx_run_result << '\n';
    res = EXIT_SUCCESS;
    std::cout << "stopping mini-mysql-server\n";
  } catch (const std::exception &e) {
    std::cerr << "exception in main: " << e.what() << '\n';
  }

  return res;
}
