#ifndef UTIL_NV_TUPLE_FROM_COMMAND_LINE_HPP
#define UTIL_NV_TUPLE_FROM_COMMAND_LINE_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <boost/lexical_cast/try_lexical_convert.hpp>

#include "util/command_line_helpers_fwd.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/nv_tuple.hpp"

#include "util/composite_name.hpp"

namespace util {

namespace detail {

template <typename T>
  requires(!derived_from_named_value_tuple<T>)
void nv_tuple_from_command_line_helper(string_view_composite_name &label,
                                       std::size_t &argument_index,
                                       util::command_line_arg_view arguments,
                                       T &obj) {
  if (!boost::conversion::try_lexical_convert(arguments[argument_index], obj)) {
    util::exception_location().raise<std::invalid_argument>(
        "unable to extract \"" + label.str() + "\" at position " +
        std::to_string(argument_index + 1U) + " from the command line");
  }
  ++argument_index;
}

template <named_value... NVPack>
void nv_tuple_from_command_line_helper(string_view_composite_name &label,
                                       std::size_t &argument_index,
                                       util::command_line_arg_view arguments,
                                       nv_tuple<NVPack...> &obj) {
  // clang-format off
  (
    (
      label.push_back(NVPack::name.sv()),
      nv_tuple_from_command_line_helper(label, argument_index, arguments,
                                        obj.NVPack::value),
      label.pop_back()
    ), ...
  );
  // clang-format on
}

} // namespace detail

template <named_value... NVPack>
void nv_tuple_from_command_line(util::command_line_arg_view arguments,
                                nv_tuple<NVPack...> &obj) {
  using nv_tuple_type = nv_tuple<NVPack...>;
  if (std::size(arguments) != nv_tuple_type::flattened_size) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid number of command line arguments");
  }

  string_view_composite_name label;
  label.reserve(nv_tuple_type::depth);
  std::size_t argument_index{0U};

  detail::nv_tuple_from_command_line_helper(label, argument_index, arguments,
                                            obj);
}

} // namespace util

#endif // UTIL_NV_TUPLE_FROM_COMMAND_LINE_HPP
