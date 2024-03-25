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

#ifndef UTIL_EXCEPTION_LOCATION_HELPERS_HPP
#define UTIL_EXCEPTION_LOCATION_HELPERS_HPP

#include <concepts>
#include <exception>
#include <source_location>

#include "util/mixin_exception_adapter.hpp"

namespace util {

template <std::derived_from<std::exception> Exception>
using location_exception_adapter =
    mixin_exception_adapter<Exception, std::source_location>;

// this class is supposed to be used as a helper only
// e.g. exception_location().raise<std::invalid_argument>(
//   "value cannot be zero")
class [[nodiscard]] exception_location {
public:
  explicit exception_location(
      std::source_location location = std::source_location::current())
      : location_{location} {}
  template <std::derived_from<std::exception> Exception, typename... TT>
  [[noreturn]] void raise(TT &&...args) const {
    using wrapped_exception = location_exception_adapter<Exception>;
    throw wrapped_exception{location_, std::forward<TT>(args)...};
  }

private:
  std::source_location location_;
};

} // namespace util

#endif // UTIL_EXCEPTION_LOCATION_HELPERS_HPP
