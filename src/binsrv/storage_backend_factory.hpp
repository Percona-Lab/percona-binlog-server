#ifndef BINSRV_STORAGE_BACKEND_FACTORY_HPP
#define BINSRV_STORAGE_BACKEND_FACTORY_HPP

#include "binsrv/basic_storage_backend_fwd.hpp"
#include "binsrv/storage_config_fwd.hpp"

namespace binsrv {

struct [[nodiscard]] storage_backend_factory {
  [[nodiscard]] static basic_storage_backend_ptr
  create(const storage_config &config);
};

} // namespace binsrv

#endif // BINSRV_STORAGE_BACKEND_FACTORY_HPP
