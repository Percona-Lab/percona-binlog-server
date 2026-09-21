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

#ifndef OPERATIONS_LOGGER_HELPERS_HPP
#define OPERATIONS_LOGGER_HELPERS_HPP

#include <cstdint>

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/encryption_config_fwd.hpp"
#include "binsrv/keyring_config_fwd.hpp"
#include "binsrv/main_config_fwd.hpp"
#include "binsrv/replication_config_fwd.hpp"
#include "binsrv/rewrite_config_fwd.hpp"
#include "binsrv/storage_config_fwd.hpp"
#include "binsrv/storage_fwd.hpp"

#include "easymysql/connection_config_fwd.hpp"
#include "easymysql/connection_fwd.hpp"
#include "easymysql/library_fwd.hpp"
#include "easymysql/ssl_config_fwd.hpp"
#include "easymysql/tls_config_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace operations {

void log_ssl_config_info(binsrv::basic_logger &logger,
                         const easymysql::ssl_config &ssl_config);
void log_tls_config_info(binsrv::basic_logger &logger,
                         const easymysql::tls_config &tls_config);
void log_connection_config_info(
    binsrv::basic_logger &logger,
    const easymysql::connection_config &connection_config);

void log_rewrite_config_info(binsrv::basic_logger &logger,
                             const binsrv::rewrite_config &rewrite_config);
void log_replication_config_info(
    binsrv::basic_logger &logger,
    const binsrv::replication_config &replication_config);

void log_keyring_config_info(binsrv::basic_logger &logger,
                             const binsrv::keyring_config &keyring_config);

void log_encryption_config_info(
    binsrv::basic_logger &logger,
    const binsrv::encryption_config &encryption_config);

void log_storage_config_info(binsrv::basic_logger &logger,
                             const binsrv::storage_config &storage_config);

void log_config_info(binsrv::basic_logger &logger,
                     const binsrv::main_config &config);

void log_storage_info(binsrv::basic_logger &logger,
                      const binsrv::storage &storage);

void log_library_info(binsrv::basic_logger &logger,
                      const easymysql::library &mysql_lib);

void log_connection_info(binsrv::basic_logger &logger,
                         const easymysql::connection &connection);

void log_replication_info(
    binsrv::basic_logger &logger, std::uint32_t server_id,
    const binsrv::storage &storage, bool verify_checksum,
    easymysql::connection_replication_mode_type blocking_mode);

void log_span_dump(binsrv::basic_logger &logger, util::const_byte_span portion);

} // namespace operations

#endif // OPERATIONS_LOGGER_HELPERS_HPP
