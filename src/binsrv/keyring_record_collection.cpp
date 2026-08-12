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

#include "binsrv/keyring_record_collection.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

#include <boost/json/parse.hpp>

#include "binsrv/keyring_record.hpp"

#include "util/exception_location_helpers.hpp"
#include "util/file_operations_helpers.hpp"
#include "util/nv_tuple_from_json.hpp"

namespace binsrv {

keyring_record_collection::keyring_record_collection(std::string_view file_name)
    : impl_{} {
  static constexpr std::size_t max_file_size{1048576U};
  const auto data = util::read_file_content(file_name, max_file_size,
                                            "keyring record collection file");
  auto json_value = boost::json::parse(data);
  util::nv_tuple_from_json(json_value, impl_);

  validate();
}

[[nodiscard]] bool
keyring_record_collection::contains_key(std::string_view key_id) const {
  return find_key_internal(key_id) != std::cend(root().get<"keys">());
}

[[nodiscard]] const keyring_record &
keyring_record_collection::get_key(std::string_view key_id) const {
  const auto fnd_it{find_key_internal(key_id)};
  if (fnd_it == std::cend(root().get<"keys">())) {
    util::exception_location().raise<std::out_of_range>(
        "key not found in the keyring record collection");
  }
  return *fnd_it;
}

void keyring_record_collection::validate() const {
  if (root().get<"version">() != expected_keyring_record_collection_version) {
    util::exception_location().raise<std::invalid_argument>(
        "unsupported keyring record collection version");
  }
  const auto &keys{root().get<"keys">()};

  using key_id_container = std::unordered_set<std::string>;
  key_id_container unique_ids{};

  for (const auto &key : keys) {
    const auto &key_id = key.get<"id">();
    if (!unique_ids.insert(key_id).second) {
      util::exception_location().raise<std::invalid_argument>(
          "duplicate key id in keyring record collection: '" + key_id + "'");
    }

    key.validate();
  }
}

[[nodiscard]] std::string keyring_record_collection::get_description() const {
  std::string result{};
  const auto &keys{root().get<"keys">()};
  result += std::to_string(keys.size());
  result += " key(s):";
  for (const auto &key : keys) {
    result += ' ';
    result += key.get_description();
  }
  return result;
}

[[nodiscard]] keyring_record_collection::key_collection::const_iterator
keyring_record_collection::find_key_internal(std::string_view key_id) const {
  const auto &keys{root().get<"keys">()};
  return std::ranges::find_if(keys, [key_id](const keyring_record &key) {
    return key.get<"id">() == key_id;
  });
}

} // namespace binsrv
