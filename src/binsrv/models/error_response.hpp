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

#ifndef BINSRV_MODELS_ERROR_RESPONSE_HPP
#define BINSRV_MODELS_ERROR_RESPONSE_HPP

#include "binsrv/models/error_response_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <string>
#include <string_view>

#include "binsrv/models/response_status_type_fwd.hpp"

#include "util/nv_tuple.hpp"

namespace binsrv::models {

class [[nodiscard]] error_response {
private:
  using impl_type = util::nv_tuple<
      // clang-format off
      util::nv<"version", std::uint32_t>,
      util::nv<"status", response_status_type>,
      util::nv<"message", std::string>
      // clang-format on
      >;

public:
  explicit error_response(std::string_view message);

  [[nodiscard]] std::string str() const;

  [[nodiscard]] auto &root() noexcept { return impl_; }

private:
  impl_type impl_;
};

} // namespace binsrv::models

#endif // BINSRV_MODELS_ERROR_RESPONSE_HPP
