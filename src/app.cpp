#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>

#include <mysql/mysql.h>

#include "binsrv/connection_config.hpp"

int main(int argc, char *argv[]) {
  int exit_code = EXIT_FAILURE;
  if (argc != binsrv::connection_config::expected_number_of_parameters + 1 &&
      argc != 2) {
    std::filesystem::path executable_path{argv[0]};
    auto filename = executable_path.filename();
    std::cerr << "usage: " << filename << " <host> <port> <user> <password>\n"
              << "       " << filename << " <json_config_file>\n";
    return exit_code;
  }

  try {
    using connection_config_ptr = std::unique_ptr<binsrv::connection_config>;
    connection_config_ptr config;
    if (argc == 2) {
      std::cout << "Reading connection configuration from the JSON file.\n";
      config = std::make_unique<binsrv::connection_config>(argv[1]);
    } else if (argc ==
               binsrv::connection_config::expected_number_of_parameters + 1) {
      std::cout << "Reading connection configuration from the command line "
                   "parameters.\n";
      binsrv::connection_config::parameter_container parameters;
      std::copy(argv + 1, argv + argc, parameters.begin());
      config = std::make_unique<binsrv::connection_config>(parameters);
    } else {
      assert(false);
    }
    assert(config);
    std::cout << "host    : " << config->get_host() << '\n';
    std::cout << "port    : " << config->get_port() << '\n';
    std::cout << "user    : " << config->get_user() << '\n';
    std::cout << "password: " << config->get_password() << '\n';

    std::cout << '\n';
    std::cout << "mysql client version: " << mysql_get_client_version() << '\n';

    exit_code = EXIT_SUCCESS;
  } catch (const std::exception &e) {
    std::cerr << "std::exception caught: " << e.what() << '\n';
  } catch (...) {
    std::cerr << "unhandled exception caught\n";
  }

  return exit_code;
}
