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

#ifndef UTIL_NV_TUPLE_TO_JSON_HPP
#define UTIL_NV_TUPLE_TO_JSON_HPP

#include <boost/lexical_cast.hpp>

#include <boost/json/value.hpp>
#include <boost/json/value_from.hpp>

#include "util/exception_location_helpers.hpp"
#include "util/nv_tuple.hpp"
#include "util/nv_tuple_json_support.hpp"

namespace util {

namespace detail {

// This class serves the purpose of ADL - it is supposed to be used in
// 'tag_invoke()' overloads in order to be able to convert instances of
// classes declared in different namespaces.
struct insertion_context {};

// The tag_invoke() overload for nv_tuple<..>.
template <named_value... NVPack>
void tag_invoke(boost::json::value_from_tag /*unused*/,
                boost::json::value &json_value, const nv_tuple<NVPack...> &obj,
                const insertion_context &insertion_ctx) {
  boost::json::object json_object{};

  (json_object.emplace(NVPack::name.sv(),
                       boost::json::value_from(obj.template get<NVPack::name>(),
                                               insertion_ctx)),
   ...);
  json_value = std::move(json_object);
}

// The tag_invoke() overload for types that can be converted to
// std::strings via boost::lexical_cast (explicitly marked as convertible
// via util::string_convertable<...> specialization).
template <string_convertable T>
void tag_invoke(boost::json::value_from_tag /*unused*/,
                boost::json::value &json_value, const T &obj,
                const insertion_context & /*unused*/) {
  json_value = boost::lexical_cast<std::string>(obj);
}

} // namespace detail

// Primary nv_tuple<..> insertion function.
template <named_value... NVPack>
void nv_tuple_to_json(boost::json::value &json_value,
                      const nv_tuple<NVPack...> &obj) {
  try {
    const detail::insertion_context insertion_ctx{};
    json_value = boost::json::value_from(obj, insertion_ctx);
  } catch (const std::exception &ex) {
    exception_location().raise<std::invalid_argument>(
        "unable to insert into JSON object");
  }
}

} // namespace util

#endif // UTIL_NV_TUPLE_TO_JSON_HPP
