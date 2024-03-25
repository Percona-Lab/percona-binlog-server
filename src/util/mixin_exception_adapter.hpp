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

#ifndef UTIL_MIXIN_EXCEPTION_ADAPTER_HPP
#define UTIL_MIXIN_EXCEPTION_ADAPTER_HPP

#include <concepts>
#include <exception>
#include <utility>

namespace util {

template <std::derived_from<std::exception> Exception, typename Mixin>
class [[nodiscard]] mixin_exception_adapter : public Exception, public Mixin {
public:
  using base_type = Exception;
  using mixin_type = Mixin;

  template <typename MixinArg, typename... TT>
  explicit mixin_exception_adapter(MixinArg &&mixin, TT &&...args)
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
      : base_type{std::forward<TT>(args)...},
        mixin_type{std::forward<MixinArg>(mixin)} {}

  [[nodiscard]] mixin_type &get_mixin() noexcept {
    return *static_cast<mixin_type *>(this);
  }
  [[nodiscard]] const mixin_type &get_mixin() const noexcept {
    return *static_cast<const mixin_type *>(this);
  }
};

} // namespace util

#endif // UTIL_MIXIN_EXCEPTION_ADAPTER_HPP
