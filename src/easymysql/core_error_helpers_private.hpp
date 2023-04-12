#ifndef EASYMYSQL_CORE_ERROR_HELPERS_PRIVATE_HPP
#define EASYMYSQL_CORE_ERROR_HELPERS_PRIVATE_HPP

#include "easymysql/connection_fwd.hpp"
#include <source_location>

namespace easymysql {

[[noreturn]] void raise_core_error_from_connection(
    const connection &conn,
    std::source_location location = std::source_location::current());

} // namespace easymysql

#endif // EASYMYSQL_CORE_ERROR_HELPERS_PRIVATE_HPP
