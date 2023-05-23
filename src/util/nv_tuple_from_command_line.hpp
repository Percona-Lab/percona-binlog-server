#ifndef UTIL_NV_TUPLE_FROM_COMMAND_LINE_HPP
#define UTIL_NV_TUPLE_FROM_COMMAND_LINE_HPP

#include <boost/lexical_cast.hpp>

#include "util/command_line_helpers_fwd.hpp"
#include "util/nv_tuple.hpp"

namespace util {

template <typename... NVPack>
void nv_tuple_from_command_line(util::command_line_arg_view arguments,
                                nv_tuple<NVPack...> &obj) {
  std::size_t index = 0;
  ((obj.NVPack::value =
        boost::lexical_cast<typename NVPack::type>(arguments[index++])),
   ...);
}

} // namespace util

#endif // UTIL_NV_TUPLE_FROM_COMMAND_LINE_HPP
