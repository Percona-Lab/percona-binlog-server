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

#include "operations/logger_helpers.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <locale>
#include <optional>
#include <string>
#include <string_view>

#include <boost/lexical_cast.hpp>

#include "binsrv/basic_logger.hpp"
#include "binsrv/encryption_config.hpp"
#include "binsrv/encryption_format_type.hpp" // IWYU pragma: keep
#include "binsrv/keyring_config.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/main_config.hpp"
#include "binsrv/replication_config.hpp"
#include "binsrv/replication_mode_type.hpp"
#include "binsrv/rewrite_config.hpp"
#include "binsrv/size_unit.hpp"
#include "binsrv/storage.hpp"
#include "binsrv/storage_backend_type.hpp" // IWYU pragma: keep
#include "binsrv/storage_config.hpp"
#include "binsrv/time_unit.hpp"

#include "easymysql/connection.hpp"
#include "easymysql/connection_config.hpp"
#include "easymysql/library.hpp"
#include "easymysql/ssl_config.hpp"
#include "easymysql/ssl_mode_type.hpp" // IWYU pragma: keep
#include "easymysql/tls_config.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/common_optional_types.hpp"
#include "util/ct_string.hpp"
#include "util/nv_tuple_fwd.hpp"

namespace operations {

namespace {

template <typename T> util::optional_string to_log_string(const T &value) {
  return boost::lexical_cast<std::string>(value);
}

util::optional_string to_log_string(const binsrv::size_unit &value) {
  return value.get_description();
}

util::optional_string to_log_string(const binsrv::time_unit &value) {
  return value.get_description();
}

util::optional_string to_log_string(bool value) {
  return {value ? "true" : "false"};
}

template <typename T>
util::optional_string to_log_string(const std::optional<T> &value) {
  if (!value.has_value()) {
    return {};
  }
  return to_log_string(*value);
}

template <util::ct_string CTS, util::derived_from_named_value_tuple Config>
void log_config_param(binsrv::basic_logger &logger, const Config &config,
                      std::string_view label) {
  const auto opt_log_string{to_log_string(config.template get<CTS>())};
  if (opt_log_string.has_value()) {
    logger.log_format(binsrv::log_severity::info, "{}: {}", label,
                      *opt_log_string);
  }
}

} // anonymous namespace

void log_ssl_config_info(binsrv::basic_logger &logger,
                         const easymysql::ssl_config &ssl_config) {
  log_config_param<"mode">(logger, ssl_config, "SSL mode");
  log_config_param<"ca">(logger, ssl_config, "SSL ca");
  log_config_param<"capath">(logger, ssl_config, "SSL capath");
  log_config_param<"crl">(logger, ssl_config, "SSL crl");
  log_config_param<"crlpath">(logger, ssl_config, "SSL crlpath");
  log_config_param<"cert">(logger, ssl_config, "SSL cert");
  log_config_param<"key">(logger, ssl_config, "SSL key");
  log_config_param<"cipher">(logger, ssl_config, "SSL cipher");
}

void log_tls_config_info(binsrv::basic_logger &logger,
                         const easymysql::tls_config &tls_config) {
  log_config_param<"ciphersuites">(logger, tls_config, "TLS ciphersuites");
  log_config_param<"version">(logger, tls_config, "TLS version");
}

void log_connection_config_info(
    binsrv::basic_logger &logger,
    const easymysql::connection_config &connection_config) {
  logger.log_format(binsrv::log_severity::info, "mysql connection string: {}",
                    connection_config.get_connection_string());

  log_config_param<"connect_timeout">(logger, connection_config,
                                      "mysql connect timeout (seconds)");
  log_config_param<"read_timeout">(logger, connection_config,
                                   "mysql read timeout (seconds)");
  log_config_param<"write_timeout">(logger, connection_config,
                                    "mysql write timeout (seconds)");

  const auto &optional_ssl_config{connection_config.get<"ssl">()};
  if (optional_ssl_config.has_value()) {
    log_ssl_config_info(logger, *optional_ssl_config);
  }
  const auto &optional_tls_config{connection_config.get<"tls">()};
  if (optional_tls_config.has_value()) {
    log_tls_config_info(logger, *optional_tls_config);
  }
}

void log_rewrite_config_info(binsrv::basic_logger &logger,
                             const binsrv::rewrite_config &rewrite_config) {
  log_config_param<"base_file_name">(logger, rewrite_config,
                                     "rewrite base binlog file name");
  log_config_param<"file_size">(logger, rewrite_config,
                                "rewrite binlog file size");
}
void log_replication_config_info(
    binsrv::basic_logger &logger,
    const binsrv::replication_config &replication_config) {

  log_config_param<"server_id">(logger, replication_config,
                                "mysql replication server id");
  log_config_param<"idle_time">(logger, replication_config,
                                "mysql replication idle time (seconds)");
  log_config_param<"verify_checksum">(
      logger, replication_config, "mysql replication checksum verification");
  log_config_param<"mode">(logger, replication_config,
                           "mysql replication mode");
  const auto &optional_rewrite_config{replication_config.get<"rewrite">()};
  if (optional_rewrite_config.has_value()) {
    log_rewrite_config_info(logger, *optional_rewrite_config);
  }
}

void log_keyring_config_info(binsrv::basic_logger &logger,
                             const binsrv::keyring_config &keyring_config) {
  log_config_param<"uri">(logger, keyring_config, "keyring URI");
}

void log_encryption_config_info(
    binsrv::basic_logger &logger,
    const binsrv::encryption_config &encryption_config) {
  log_config_param<"format">(logger, encryption_config,
                             "binlog storage encryption format");
  log_config_param<"kek_id">(logger, encryption_config,
                             "binlog storage encryption KEK identifier");
  log_config_param<"cipher">(logger, encryption_config,
                             "binlog storage encryption data cipher");
}

void log_storage_config_info(binsrv::basic_logger &logger,
                             const binsrv::storage_config &storage_config) {

  log_config_param<"backend">(logger, storage_config,
                              "binlog storage backend type");
  logger.log_format(binsrv::log_severity::info,
                    "binlog storage backend URI (masked): {}",
                    storage_config.get_masked_uri());
  log_config_param<"fs_buffer_directory">(
      logger, storage_config,
      "binlog storage backend filesystem buffer directory");
  log_config_param<"checkpoint_size">(
      logger, storage_config, "binlog storage backend checkpointing size");
  log_config_param<"checkpoint_interval">(
      logger, storage_config, "binlog storage backend checkpointing interval");
  const auto &optional_encryption_config{storage_config.get<"encryption">()};
  if (optional_encryption_config.has_value()) {
    log_encryption_config_info(logger, *optional_encryption_config);
  }
}

void log_config_info(binsrv::basic_logger &logger,
                     const binsrv::main_config &config) {
  const auto &keyring_config{config.root().get<"keyring">()};
  if (keyring_config.has_value()) {
    log_keyring_config_info(logger, *keyring_config);
  } else {
    logger.log(binsrv::log_severity::info,
               "keyring configuration options are not specified");
  }

  const auto &storage_config{config.root().get<"storage">()};
  log_storage_config_info(logger, storage_config);

  const auto &connection_config{config.root().get<"connection">()};
  log_connection_config_info(logger, connection_config);

  const auto &replication_config{config.root().get<"replication">()};
  log_replication_config_info(logger, replication_config);
}

void log_storage_info(binsrv::basic_logger &logger,
                      const binsrv::storage &storage) {
  logger.log_format(binsrv::log_severity::info,
                    "created binlog storage with the following backend: {}",
                    storage.get_backend_description());
  logger.log_format(
      binsrv::log_severity::info, "binlog storage initialized in {} mode",
      boost::lexical_cast<std::string>(storage.get_replication_mode()));

  if (storage.is_empty()) {
    logger.log(binsrv::log_severity::info,
               "binlog storage initialized on an empty directory");
  } else {
    logger.log_format(binsrv::log_severity::info,
                      "binlog storage initialized at \"{}\":{}",
                      storage.get_current_binlog_name().str(),
                      storage.get_current_position());
  }
  logger.log_format(binsrv::log_severity::info, "storage keyring status: {}",
                    storage.get_keyring_description());
  logger.log_format(binsrv::log_severity::info, "storage active KEK: {}",
                    storage.get_active_kek_description());
  logger.log_format(binsrv::log_severity::info, "storage encryption format: {}",
                    storage.get_encryption_format_description());
}

void log_library_info(binsrv::basic_logger &logger,
                      const easymysql::library &mysql_lib) {
  logger.log(binsrv::log_severity::info, "initialized mysql client library");
  logger.log_format(binsrv::log_severity::info, "mysql client version: {}",
                    mysql_lib.get_readable_client_version());
}

void log_connection_info(binsrv::basic_logger &logger,
                         const easymysql::connection &connection) {
  logger.log_format(binsrv::log_severity::info, "mysql server version: {}",
                    connection.get_readable_server_version());
  logger.log_format(binsrv::log_severity::info, "mysql protocol version: {}",
                    connection.get_protocol_version());
  logger.log_format(binsrv::log_severity::info,
                    "mysql server connection info: {}",
                    connection.get_server_connection_info());
  logger.log_format(binsrv::log_severity::info,
                    "mysql connection character set: {}",
                    connection.get_character_set_name());
}

void log_replication_info(
    binsrv::basic_logger &logger, std::uint32_t server_id,
    const binsrv::storage &storage, bool verify_checksum,
    easymysql::connection_replication_mode_type blocking_mode) {
  const auto replication_mode{storage.get_replication_mode()};

  logger.log_format(binsrv::log_severity::info,
                    "switched to replication (checksum {}, {} mode)",
                    (verify_checksum ? "enabled" : "disabled"),
                    boost::lexical_cast<std::string>(replication_mode));

  std::string starting_from;
  if (replication_mode == binsrv::replication_mode_type::position) {
    if (storage.is_empty()) {
      starting_from = "the very beginning";
    } else {
      starting_from =
          std::format("{}:{}", storage.get_current_binlog_name().str(),
                      storage.get_current_position());
    }
  } else {
    const auto &gtids{storage.get_gtids()};
    if (gtids.is_empty()) {
      starting_from = "an empty GTID set";
    } else {
      starting_from = std::format("the {} GTID set",
                                  boost::lexical_cast<std::string>(gtids));
    }
  }
  logger.log_format(
      binsrv::log_severity::info,
      "replication info (server id {}, {}, starting from {})", server_id,
      (blocking_mode == easymysql::connection_replication_mode_type::blocking
           ? "blocking"
           : "non-blocking"),
      starting_from);
}

void log_span_dump(binsrv::basic_logger &logger,
                   util::const_byte_span portion) {
  logger.log_format(binsrv::log_severity::debug,
                    "fetched {}-byte(s) event from binlog", std::size(portion));
  // explicitly checking log level and return early as computing
  // hex dump is not a trivial operation
  if (logger.get_min_level() > binsrv::log_severity::trace) {
    return;
  }
  static constexpr auto bytes_per_dump_line{16UZ};
  auto offset{0UZ};
  while (offset < std::size(portion)) {
    auto sub = portion.subspan(
        offset, std::min(bytes_per_dump_line, std::size(portion) - offset));

    std::string line{"["};
    for (auto current_byte : sub) {
      std::format_to(std::back_inserter(line), " {:02x}",
                     std::to_integer<std::uint8_t>(current_byte));
    }
    line.append((bytes_per_dump_line - std::size(sub)) * 3U, ' ');
    line += " ] ";

    const auto &ctype_facet{
        std::use_facet<std::ctype<char>>(std::locale::classic())};
    for (auto current_byte : sub) {
      auto current_char{std::to_integer<char>(current_byte)};
      if (!ctype_facet.is(std::ctype_base::print, current_char)) {
        current_char = '.';
      }
      line += current_char;
    }
    logger.log(binsrv::log_severity::trace, line);
    offset += bytes_per_dump_line;
  }
}

} // namespace operations
