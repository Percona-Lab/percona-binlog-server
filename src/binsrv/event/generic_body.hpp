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

#ifndef BINSRV_EVENT_GENERIC_BODY_HPP
#define BINSRV_EVENT_GENERIC_BODY_HPP

#include "binsrv/event/generic_body_fwd.hpp" // IWYU pragma: export

#include "binsrv/event/unknown_body_fwd.hpp"

#include "util/redirectable.hpp"

namespace binsrv::event {

// This class template is expected to be specialized for individual event type
// codes. The specialization should be constructible from `const_byte_span`.
// template<> class generic_body_impl<code_type::xxx> {
// public:
//   generic_body_impl<code_type::xxx>(util::const_byte_span portion);
// ...
// };
// Alternatively, the implementation can be "redirected" to another (already
// existing) class via inner 'redirect_type' type alias.
// template<> class generic_body_impl<code_type::yyy> {
// public:
//   using redirect_type = empty_body;
// ...
// };
// Currently, only two classes 'empty_body' and 'unknown_body'
// are expected to be used for redirection.
// By default, everything is redirected to 'unknown_body' class.
template <code_type Code> class generic_body_impl {
public:
  using redirect_type = unknown_body;
};

template <code_type Code> struct generic_body_helper {
  using type = generic_body_impl<Code>;
};

template <code_type Code>
  requires util::redirectable<generic_body_impl<Code>>
struct generic_body_helper<Code> {
  using type = typename generic_body_impl<Code>::redirect_type;
};

template <code_type Code>
using generic_body = typename generic_body_helper<Code>::type;

} // namespace binsrv::event

#endif // BINSRV_EVENT_GENERIC_BODY_HPP
