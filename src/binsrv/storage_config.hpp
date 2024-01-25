#ifndef BINSRV_STORAGE_CONFIG_HPP
#define BINSRV_STORAGE_CONFIG_HPP

#include "binsrv/storage_config_fwd.hpp" // IWYU pragma: export

#include <string>

#include "util/nv_tuple.hpp"

namespace binsrv {

// clang-format off
struct [[nodiscard]] storage_config
    : util::nv_tuple<
          util::nv<"path", std::string>
      > {};
// clang-format on

} // namespace binsrv

#endif // BINSRV_STORAGE_CONFIG_HPP
