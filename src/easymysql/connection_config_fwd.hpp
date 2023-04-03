#ifndef EASYMYSQL_CONNECTION_CONFIG_FWD_HPP
#define EASYMYSQL_CONNECTION_CONFIG_FWD_HPP

#include <memory>

namespace easymysql {

class connection_config;

using connection_config_ptr = std::shared_ptr<connection_config>;

} // namespace easymysql

#endif // EASYMYSQL_CONNECTION_CONFIG_FWD_HPP
