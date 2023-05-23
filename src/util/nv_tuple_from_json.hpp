#ifndef UTIL_NV_TUPLE_FROM_JSON_HPP
#define UTIL_NV_TUPLE_FROM_JSON_HPP

#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>

#include "util/nv_tuple.hpp"

namespace util {

template <typename... NVPack>
void nv_tuple_from_json(const boost::json::value &json_value,
                        nv_tuple<NVPack...> &obj) {
  const auto &json_object = json_value.as_object();
  ((obj.NVPack::value = boost::json::value_to<typename NVPack::type>(
        json_object.at(NVPack::name.sv()))),
   ...);
}

} // namespace util

#endif // UTIL_NV_TUPLE_FROM_JSON_HPP
