#ifndef BINSRV_EVENT_FLAG_FWD_HPP
#define BINSRV_EVENT_FLAG_FWD_HPP

#include <cstdint>

#include "util/flag_set_fwd.hpp"

namespace binsrv {

enum class event_flag : std::uint16_t;

using event_flag_set = util::flag_set<event_flag>;

} // namespace binsrv

#endif // BINSRV_EVENT_FLAG_FWD_HPP
