#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <mysql/mysql.h>

#include "binsrv/connection_config.hpp"
#include "binsrv/ostream_logger.hpp"

#include "util/command_line_helpers.hpp"

[[nodiscard]] std::string get_readable_mysql_client_version() {
  static constexpr std::uint32_t version_multiplier = 100U;
  auto mysql_client_version =
      static_cast<std::uint32_t>(mysql_get_client_version());
  const std::uint32_t mysql_client_version_patch =
      mysql_client_version % version_multiplier;
  mysql_client_version /= version_multiplier;
  const std::uint32_t mysql_client_version_minor =
      mysql_client_version % version_multiplier;
  mysql_client_version /= version_multiplier;
  const std::uint32_t mysql_client_version_major =
      mysql_client_version % version_multiplier;

  std::string res;
  res += std::to_string(mysql_client_version_major);
  res += '.';
  res += std::to_string(mysql_client_version_minor);
  res += '.';
  res += std::to_string(mysql_client_version_patch);

  return res;
}

int main(int argc, char *argv[]) {
  using namespace std::string_literals;

  int exit_code = EXIT_FAILURE;

  const auto cmd_args = util::to_command_line_agg_view(argc, argv);
  const auto number_of_cmd_args = std::size(cmd_args);

  if (number_of_cmd_args !=
          binsrv::connection_config::expected_number_of_arguments + 1 &&
      number_of_cmd_args != 2) {
    auto executable_name = util::extract_executable_name(cmd_args);
    std::cerr << "usage: " << executable_name
              << " <host> <port> <user> <password>\n"
              << "       " << executable_name << " <json_config_file>\n";
    return exit_code;
  }
  binsrv::basic_logger_ptr logger;

  try {
    const auto default_log_level = binsrv::log_severity::trace;
    logger = std::make_shared<binsrv::ostream_logger>(std::cout);
    logger->set_min_level(default_log_level);
    const auto log_level_label =
        binsrv::to_string_view(logger->get_min_level());
    // logging with "delimiter" level has the highest priority and empty label
    logger->log(binsrv::log_severity::delimiter,
                "logging level set to \""s + std::string{log_level_label} +
                    '"');

    binsrv::connection_config_ptr config;
    if (number_of_cmd_args == 2) {
      logger->log(binsrv::log_severity::info,
                  "Reading connection configuration from the JSON file.");
      config = std::make_shared<binsrv::connection_config>(cmd_args[1]);
    } else if (number_of_cmd_args ==
               binsrv::connection_config::expected_number_of_arguments + 1) {
      logger->log(binsrv::log_severity::info,
                  "Reading connection configuration from the command line "
                  "parameters.");
      config = std::make_shared<binsrv::connection_config>(cmd_args);
    } else {
      assert(false);
    }
    assert(config);

    logger->log(
        binsrv::log_severity::info,
        "mysql connection string: " + config->get_user() + '@' +
            config->get_host() + ':' + std::to_string(config->get_port()) +
            (config->get_password().empty() ? " (no password)"
                                            : " (password ***hidden***)"));

    logger->log(binsrv::log_severity::info,
                "mysql client version: " + get_readable_mysql_client_version());

    exit_code = EXIT_SUCCESS;
  } catch (const std::exception &e) {
    if (logger) {
      logger->log(binsrv::log_severity::error,
                  "std::exception caught: "s + e.what());
    }
  } catch (...) {
    if (logger) {
      logger->log(binsrv::log_severity::info, "unhandled exception caught");
    }
  }

  return exit_code;
}
