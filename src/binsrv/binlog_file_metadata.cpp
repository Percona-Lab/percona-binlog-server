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

#include "binsrv/binlog_file_metadata.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <boost/algorithm/hex.hpp>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>

// needed for binsrv::gtids::gtid_set_storage
#include <boost/container/small_vector.hpp> // IWYU pragma: keep

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "util/byte_span.hpp"
#include "util/nv_tuple_from_json.hpp"
#include "util/nv_tuple_to_json.hpp"

namespace binsrv {

binlog_file_metadata::binlog_file_metadata()
    : impl_{{expected_binlog_file_metadata_version}, {}, {}} {}

binlog_file_metadata::binlog_file_metadata(std::string_view data) : impl_{} {
  auto json_value = boost::json::parse(data);
  util::nv_tuple_from_json(json_value, impl_);

  validate();
}

[[nodiscard]] std::string binlog_file_metadata::str() const {
  boost::json::value json_value;
  util::nv_tuple_to_json(json_value, impl_);

  return boost::json::serialize(json_value);
}

[[nodiscard]] gtids::gtid_set binlog_file_metadata::get_gtids() const {
  const auto &optional_gtids{root().get<"gtids">()};
  if (!optional_gtids.has_value()) {
    return {};
  }

  const auto &encoded_gtids{optional_gtids.value()};
  std::string decoded_gtids(std::size(encoded_gtids) / 2U, 'x');
  boost::algorithm::unhex(encoded_gtids, std::data(decoded_gtids));
  return gtids::gtid_set{util::as_const_byte_span(decoded_gtids)};
}

void binlog_file_metadata::set_gtids(const gtids::gtid_set &gtids) {
  const auto encoded_size{gtids.calculate_encoded_size()};

  gtids::gtid_set_storage buffer(encoded_size);
  util::byte_span destination{buffer};
  gtids.encode_to(destination);

  std::string encoded_gtids(std::size(buffer) * 2U, 'x');
  const auto buffer_sv{util::as_string_view(std::as_const(buffer))};
  boost::algorithm::hex_lower(buffer_sv, std::data(encoded_gtids));
  root().get<"gtids">().emplace(std::move(encoded_gtids));
}

void binlog_file_metadata::validate() const {
  if (root().get<"version">() != expected_binlog_file_metadata_version) {
    util::exception_location().raise<std::invalid_argument>(
        "unsupported binlog file metadata version");
  }
}

} // namespace binsrv
