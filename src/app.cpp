#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "binsrv/exception_handling_helpers.hpp"
#include "binsrv/ostream_logger.hpp"

#include "easymysql/binlog.hpp"
#include "easymysql/connection.hpp"
#include "easymysql/connection_config.hpp"
#include "easymysql/library.hpp"

#include "util/command_line_helpers.hpp"

int main(int argc, char *argv[]) {
  using namespace std::string_literals;

  int exit_code = EXIT_FAILURE;

  const auto cmd_args = util::to_command_line_agg_view(argc, argv);
  const auto number_of_cmd_args = std::size(cmd_args);

  if (number_of_cmd_args !=
          easymysql::connection_config::expected_number_of_arguments + 1 &&
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

    easymysql::connection_config_ptr config;
    if (number_of_cmd_args == 2) {
      logger->log(binsrv::log_severity::info,
                  "Reading connection configuration from the JSON file.");
      config = std::make_shared<easymysql::connection_config>(cmd_args[1]);
    } else if (number_of_cmd_args ==
               easymysql::connection_config::expected_number_of_arguments + 1) {
      logger->log(binsrv::log_severity::info,
                  "Reading connection configuration from the command line "
                  "parameters.");
      config = std::make_shared<easymysql::connection_config>(cmd_args);
    } else {
      assert(false);
    }
    assert(config);

    logger->log(binsrv::log_severity::info,
                "mysql connection string: " + config->get_connection_string());

    const easymysql::library mysql_lib;
    logger->log(binsrv::log_severity::info, "initialized mysql client library");

    std::string msg = "mysql client version: ";
    msg += mysql_lib.get_readable_client_version();
    logger->log(binsrv::log_severity::info, msg);

    auto connection = mysql_lib.create_connection(*config);
    logger->log(binsrv::log_severity::info,
                "established connection to mysql server");
    msg = "mysql server version: ";
    msg += connection.get_readable_server_version();
    logger->log(binsrv::log_severity::info, msg);

    logger->log(binsrv::log_severity::info,
                "mysql protocol version: " +
                    std::to_string(connection.get_protocol_version()));

    msg = "mysql server connection info: ";
    msg += connection.get_server_connection_info();
    logger->log(binsrv::log_severity::info, msg);

    msg = "mysql connection character set: ";
    msg += connection.get_character_set_name();
    logger->log(binsrv::log_severity::info, msg);

    static constexpr std::uint32_t default_server_id = 0;
    auto binlog = connection.create_binlog(default_server_id);
    logger->log(binsrv::log_severity::info, "opened binary log connection");

    easymysql::binlog_stream_span portion;
    while (!(portion = binlog.fetch()).empty()) {
      logger->log(binsrv::log_severity::info,
                  "fetched " + std::to_string(portion.size()) +
                      "-byte(s) data chunk from binlog");
    }

    exit_code = EXIT_SUCCESS;
  } catch (...) {
    handle_std_exception(logger);
  }

  return exit_code;
}
