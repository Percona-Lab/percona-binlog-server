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

#ifndef BINSRV_EVENT_ROTATE_BODY_IMPL_HPP
#define BINSRV_EVENT_ROTATE_BODY_IMPL_HPP

#include "binsrv/event/rotate_body_impl_fwd.hpp" // IWYU pragma: export

#include <string>
#include <string_view>

#include <boost/container/small_vector.hpp>

#include "util/byte_span.hpp"

namespace binsrv::event {

template <> class [[nodiscard]] generic_body_impl<code_type::rotate> {
public:
  static constexpr std::size_t expected_max_binlog_name_length{64U};
  // TODO: in c++26 change to std::inplace_vector
  using binlog_storage =
      boost::container::small_vector<std::byte,
                                     expected_max_binlog_name_length>;

  explicit generic_body_impl(util::const_byte_span portion);

  [[nodiscard]] const binlog_storage &get_binlog_raw() noexcept {
    return binlog_;
  }

  [[nodiscard]] std::string_view get_binlog() const noexcept {
    return util::as_string_view(binlog_);
  }

private:
  binlog_storage binlog_{};
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_ROTATE_BODY_IMPL_HPP
