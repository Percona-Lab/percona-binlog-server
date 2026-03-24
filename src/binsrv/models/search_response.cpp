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

#include "binsrv/models/search_response.hpp"

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>

#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>

#include "binsrv/gtids/gtid_set.hpp"

#include "binsrv/models/binlog_file_record.hpp"
#include "binsrv/models/response_status_type.hpp"

#include "util/nv_tuple_to_json.hpp"

namespace binsrv::models {

search_response::search_response()
    : impl_{{expected_search_response_version},
            {response_status_type::success},
            {}} {}

search_response::search_response(const search_response &) = default;
search_response::search_response(search_response &&) noexcept = default;
search_response &search_response::operator=(const search_response &) = default;
search_response &
search_response::operator=(search_response &&) noexcept = default;
search_response::~search_response() = default;

[[nodiscard]] std::string search_response::str() const {
  boost::json::value json_value;
  util::nv_tuple_to_json(json_value, impl_);

  return boost::json::serialize(json_value);
}

void search_response::add_record(std::string_view name, std::uint64_t size,
                                 std::string_view uri,
                                 gtids::optional_gtid_set previous_gtids,
                                 gtids::optional_gtid_set added_gtids,
                                 std::time_t min_timestamp,
                                 std::time_t max_timestamp) {
  binlog_file_record record{{{std::string{name}},
                             {size},
                             {std::string{uri}},
                             {std::move(previous_gtids)},
                             {std::move(added_gtids)},
                             {util::ctime_timestamp{min_timestamp}},
                             {util::ctime_timestamp{max_timestamp}}}};
  impl_.template get<"result">().emplace_back(std::move(record));
}

} // namespace binsrv::models
