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

#ifndef UTIL_NV_TUPLE_FROM_JSON_HPP
#define UTIL_NV_TUPLE_FROM_JSON_HPP

#include <boost/lexical_cast.hpp>

#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>

#include "util/composite_name.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/nv_tuple.hpp"
#include "util/nv_tuple_json_support.hpp"

namespace util {

namespace detail {

// A helper class that stores a pointer to a stack of labels used as a path
// in JSON hierarchy for diagnostic. An instance of this class is supposed to
// be used as a 'Context' parameter for 'tag_invoke()' functions. As these
// functions accept Context by a const reference, we use a trick here with
// another level of indirection via a pointer to a non-const
// 'string_view_composite_name' and declaring all modyfying methods of this
// class as const.
// This class also serves the purpose of ADL - it is supposed to be used in
// 'tag_invoke()' overloads in order to be able to convert instances of
// classes declared in different namespaces.
class extraction_context {
public:
  explicit extraction_context(string_view_composite_name &label_stack) noexcept
      : label_stack_{&label_stack} {}

  void push_back(std::string_view label) const {
    label_stack_->push_back(label);
  }
  void pop_back() const noexcept { label_stack_->pop_back(); }
  [[nodiscard]] std::string to_string() const {
    if (label_stack_->is_empty()) {
      return "<root_element>";
    }
    return label_stack_->str();
  }

private:
  string_view_composite_name *label_stack_;
};

// A helper function that extracts a single nv value from the specified JSON
// object and is used by the tag_invoke() overload for nv_tuple<..>.
template <named_value NV>
auto extract_element_from_json_value(const boost::json::object &json_object,
                                     const extraction_context &extraction_ctx) {
  const auto subvalue_it{json_object.find(NV::name.sv())};
  const auto &json_subvalue =
      (subvalue_it != json_object.end() ? subvalue_it->value()
                                        : boost::json::value{});

  using element_type = typename NV::type;
  // If an exception is thrown in the following 'value_to()' call, the
  // 'extraction_ctx' will still hold the JSON path to the element that
  // could not be converted.
  extraction_ctx.push_back(NV::name.sv());
  auto res{boost::json::value_to<element_type>(json_subvalue, extraction_ctx)};
  extraction_ctx.pop_back();
  return res;
}

// The tag_invoke() overload for nv_tuple<..>.
template <named_value... NVPack>
nv_tuple<NVPack...>
tag_invoke(boost::json::value_to_tag<nv_tuple<NVPack...>> /*unused*/,
           const boost::json::value &json_value,
           const extraction_context &extraction_ctx) {
  const boost::json::object &json_object = json_value.as_object();
  return nv_tuple<NVPack...>{{extract_element_from_json_value<NVPack>(
      json_object, extraction_ctx)}...};
}

// The tag_invoke() overload for classes derived from nv_tuple<..>.
// Currently these derived classes are not supposed to have any extra members.
template <derived_from_named_value_tuple T>
T tag_invoke(boost::json::value_to_tag<T> /*unused*/,
             const boost::json::value &json_value,
             const extraction_context &extraction_ctx) {
  return T{boost::json::value_to<extract_named_value_tuple_base_t<T>>(
      json_value, extraction_ctx)};
}

// The tag_invoke() overload for types that can be converted from
// std::strings via boost::lexical_cast (explicitly marked as convertible
// via util::string_convertable<...> specialization).
template <string_convertable T>
T tag_invoke(boost::json::value_to_tag<T> /*unused*/,
             const boost::json::value &json_value,
             const extraction_context & /*extraction_ctx*/) {
  const auto &value_str = json_value.as_string();
  return boost::lexical_cast<T>(static_cast<std::string_view>(value_str));
}

} // namespace detail

// Primary nv_tuple<..> extraction function.
template <named_value... NVPack>
void nv_tuple_from_json(const boost::json::value &json_value,
                        nv_tuple<NVPack...> &obj) {
  string_view_composite_name label_stack{};
  const detail::extraction_context extraction_ctx{label_stack};
  try {
    // Here we use detail::extraction_context in order to be able to work with
    // classes declared in different namespace but for whom tag_invoke()
    // functions were declared in the util::detail namespace.
    obj =
        boost::json::value_to<nv_tuple<NVPack...>>(json_value, extraction_ctx);
  } catch (const std::exception &ex) {
    util::exception_location().raise<std::invalid_argument>(
        "unable to extract \"" + extraction_ctx.to_string() + "\"");
  }
}

} // namespace util

#endif // UTIL_NV_TUPLE_FROM_JSON_HPP
