#ifndef BINSRV_EVENT_FLAG_TYPE_FWD_HPP
#define BINSRV_EVENT_FLAG_TYPE_FWD_HPP

#include <cstdint>

#include "util/flag_set_fwd.hpp"

namespace binsrv::event {

enum class flag_type : std::uint16_t;

using flag_set = util::flag_set<flag_type>;

} // namespace binsrv::event

#endif // BINSRV_EVENT_FLAG_TYPE_FWD_HPP
