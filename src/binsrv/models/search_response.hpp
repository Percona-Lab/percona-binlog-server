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

#ifndef BINSRV_MODELS_SEARCH_RESPONSE_HPP
#define BINSRV_MODELS_SEARCH_RESPONSE_HPP

#include "binsrv/models/search_response_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "binsrv/gtids/gtid_set_fwd.hpp"

#include "binsrv/models/binlog_file_record_fwd.hpp"
#include "binsrv/models/response_status_type_fwd.hpp"

#include "util/nv_tuple.hpp"

namespace binsrv::models {

class [[nodiscard]] search_response {
private:
  using binlog_file_record_container = std::vector<binlog_file_record>;

  using impl_type = util::nv_tuple<
      // clang-format off
      util::nv<"version", std::uint32_t>,
      util::nv<"status", response_status_type>,
      util::nv<"result", binlog_file_record_container>
      // clang-format on
      >;

public:
  explicit search_response();
  search_response(const search_response &);
  search_response(search_response &&) noexcept;
  search_response &operator=(const search_response &);
  search_response &operator=(search_response &&) noexcept;
  ~search_response();

  [[nodiscard]] std::string str() const;

  [[nodiscard]] auto &root() noexcept { return impl_; }

  // 'previous_gtids' and 'added_gtids' are deliberately taken by value as we
  // are going to move from them
  void add_record(std::string_view name, std::uint64_t size,
                  std::string_view uri, gtids::optional_gtid_set previous_gtids,
                  gtids::optional_gtid_set added_gtids,
                  std::time_t min_timestamp, std::time_t max_timestamp);

private:
  impl_type impl_;
};

} // namespace binsrv::models

#endif // BINSRV_MODELS_SEARCH_RESPONSE_HPP
