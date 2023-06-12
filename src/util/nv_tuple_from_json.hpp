#ifndef UTIL_NV_TUPLE_FROM_JSON_HPP
#define UTIL_NV_TUPLE_FROM_JSON_HPP

#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <boost/lexical_cast.hpp>

#include "util/composite_name.hpp"
#include "util/mixin_exception_adapter.hpp"
#include "util/nv_tuple.hpp"

namespace util {

namespace detail {

using annotated_json_extract_exception =
    mixin_exception_adapter<std::invalid_argument, string_view_composite_name>;

template <named_value NV>
auto extract_element_from_json_value(const boost::json::object &json_object) {
  const auto *json_subvalue = json_object.if_contains(NV::name.sv());
  if (json_subvalue == nullptr) {
    throw annotated_json_extract_exception{NV::name.sv(),
                                           "path does not exist"};
  }

  using element_type = typename NV::type;
  try {
    if constexpr (is_derived_from_named_value_tuple_v<element_type>) {
      return boost::json::value_to<
          extract_named_value_tuple_base_t<element_type>>(*json_subvalue);
    } else if constexpr (std::is_enum_v<element_type>) {
      const auto &value_str = json_subvalue->as_string();
      return boost::lexical_cast<element_type>(
          static_cast<std::string_view>(value_str));
    } else {
      return boost::json::value_to<element_type>(*json_subvalue);
    }
  } catch (annotated_json_extract_exception &ex) {
    // catching exception here by non-const reference to be
    // able to add more info in it
    ex.push_back(NV::name.sv());
    throw;
  } catch (const std::exception &) {
    throw annotated_json_extract_exception{NV::name.sv(),
                                           "cannot extract element"};
  }
}

template <named_value... NVPack>
nv_tuple<NVPack...>
tag_invoke(boost::json::value_to_tag<nv_tuple<NVPack...>> /*unused*/,
           const boost::json::value &json_value) {
  if (!json_value.is_object()) {
    throw annotated_json_extract_exception{string_view_composite_name{},
                                           "element is not a json object"};
  }
  const boost::json::object &json_object = json_value.as_object();
  return nv_tuple<NVPack...>{
      {extract_element_from_json_value<NVPack>(json_object)}...};
}

} // namespace detail

using detail::tag_invoke;

template <named_value... NVPack>
void nv_tuple_from_json(const boost::json::value &json_value,
                        nv_tuple<NVPack...> &obj) {
  try {
    obj = boost::json::value_to<nv_tuple<NVPack...>>(json_value);
  } catch (const detail::annotated_json_extract_exception &ex) {
    std::string label = ex.str_reverse();
    if (label.empty()) {
      label = "<root_element>";
    }
    util::exception_location().raise<std::invalid_argument>(
        "unable to extract \"" + label + "\" - " + ex.what());
  }
}

} // namespace util

#endif // UTIL_NV_TUPLE_FROM_JSON_HPP
