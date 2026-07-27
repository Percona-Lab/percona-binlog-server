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

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"

#include <boost/asio/io_context.hpp>

#pragma GCC diagnostic pop

#include <boost/asio/signal_set.hpp>

#include "minimysql/network_service.hpp"
#include "minimysql/ssl_acceptor_context.hpp"

namespace {

struct parsed_cli_options {
  std::optional<std::string> ssl_cert;
  std::optional<std::string> ssl_key;
};

std::optional<parsed_cli_options>
parse_command_line(std::span<const char *const> args) {
  // Not noexcept because std::string_view::substr() may throw
  // std::out_of_range on invalid inputs. We know it cannot in practice here
  // (the passed index is always valid).
  const auto executable_basename{[](std::string_view path) -> std::string_view {
    const auto slash{path.find_last_of('/')};
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
  }};
  const std::string_view executable_name{
      args.empty() ? std::string_view{"minimysql_server"}
                   : executable_basename(args.front())};

  const auto print_usage{[executable_name](std::ostream &stream) {
    stream
        << "usage: " << executable_name
        << " [--ssl-cert=<path>] [--ssl-key=<path>]\n"
        << "  --ssl-cert / --ssl-key must be provided together to enable TLS;\n"
        << "  when both are omitted the server accepts plaintext connections\n"
        << "  only.\n";
  }};

  parsed_cli_options options{};

  for (std::size_t i{1UZ}; i < args.size(); ++i) {
    const std::string_view arg{args[i]};

    static constexpr std::string_view ssl_cert_prefix{"--ssl-cert="};
    static constexpr std::string_view ssl_key_prefix{"--ssl-key="};

    if (arg.starts_with(ssl_cert_prefix)) {
      options.ssl_cert = std::string{arg.substr(std::size(ssl_cert_prefix))};
    } else if (arg.starts_with(ssl_key_prefix)) {
      options.ssl_key = std::string{arg.substr(std::size(ssl_key_prefix))};
    } else if (arg == "--help" || arg == "-h") {
      print_usage(std::cout);
      return std::nullopt;
    } else {
      std::cerr << executable_name << ": unrecognised argument '" << arg
                << "'\n";
      print_usage(std::cerr);
      return std::nullopt;
    }
  }

  if (options.ssl_cert.has_value() != options.ssl_key.has_value() ||
      (options.ssl_cert.has_value() && options.ssl_cert->empty()) ||
      (options.ssl_key.has_value() && options.ssl_key->empty())) {
    std::cerr << executable_name
              << ": --ssl-cert and --ssl-key must be provided together and "
                 "must be non-empty\n";
    print_usage(std::cerr);
    return std::nullopt;
  }

  return options;
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char *argv[]) {
  static constexpr std::uint16_t listening_port{3307};

  static constexpr std::string_view default_username{"rpl"};
  static constexpr std::string_view default_password{"password"};
  // Optional server RSA key paths for caching_sha2_password full auth; empty
  // uses embedded defaults until wired from binlog server config.
  static constexpr std::string_view default_server_rsa_public_key_path{};
  static constexpr std::string_view default_server_rsa_private_key_path{};

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  const std::span<const char *const> args(const_cast<const char **>(argv),
                                          static_cast<std::size_t>(argc));

  const auto options{parse_command_line(args)};
  if (!options.has_value()) {
    return EXIT_FAILURE;
  }

  int res{EXIT_FAILURE};
  try {
    // Build the SSL acceptor context in main (may throw on bad cert / key /
    // mismatched pair) and then move it into network_service, which takes
    // ownership. Any construction failure surfaces here — before we ever
    // touch the network layer — instead of from network_service's
    // constructor.
    std::unique_ptr<minimysql::ssl_acceptor_context> ssl_ctx;
    if (options->ssl_cert.has_value() && options->ssl_key.has_value()) {
      ssl_ctx = std::make_unique<minimysql::ssl_acceptor_context>(
          *options->ssl_cert, *options->ssl_key);
      std::cout << "SSL enabled (cert=" << *options->ssl_cert
                << ", key=" << *options->ssl_key << ")\n";
    } else {
      std::cout << "SSL disabled (no --ssl-cert/--ssl-key provided)\n";
    }

    std::cout << "starting mini-mysql-server\n";
    boost::asio::io_context ctx;
    const minimysql::network_service service(
        ctx, listening_port, default_username, default_password,
        default_server_rsa_public_key_path, default_server_rsa_private_key_path,
        std::move(ssl_ctx));

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
