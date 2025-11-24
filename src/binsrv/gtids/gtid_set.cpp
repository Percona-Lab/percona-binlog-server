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

#include "binsrv/gtids/gtid_set.hpp"

#include <iterator>
#include <ostream>
#include <stdexcept>
#include <string_view>

#include <boost/icl/concept/interval.hpp>
#include <boost/icl/concept/interval_set.hpp>

#include <boost/uuid/uuid_io.hpp>

#include "binsrv/gtids/gtid.hpp"
#include "binsrv/gtids/tag.hpp"

#include "util/exception_location_helpers.hpp"

namespace binsrv::gtids {

gtid_set::gtid_set() = default;
gtid_set::gtid_set(const gtid_set &other) = default;
gtid_set::gtid_set(gtid_set &&other) noexcept = default;
gtid_set &gtid_set::operator=(const gtid_set &other) = default;
gtid_set &gtid_set::operator=(gtid_set &&other) noexcept = default;
gtid_set::~gtid_set() = default;

gtid_set &gtid_set::operator+=(const gtid &value) {
  if (value.is_empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot add an empty gtid");
  }
  data_[value.get_uuid()][value.get_tag()] += value.get_gno();
  return *this;
}

[[nodiscard]] bool gtid_set::contains(const gtid &value) const noexcept {
  const auto uuid_it{data_.find(value.get_uuid())};
  if (uuid_it == std::cend(data_)) {
    return false;
  }

  const auto &tagged_gnos{uuid_it->second};
  const auto tag_it{tagged_gnos.find(value.get_tag())};
  if (tag_it == std::cend(tagged_gnos)) {
    return false;
  }

  const auto &gnos{tag_it->second};
  return boost::icl::contains(gnos, value.get_gno());
}

gtid_set &gtid_set::operator+=(const gtid_set &values) {
  for (const auto &[current_uuid, current_tagged_gnos] : values.data_) {
    for (const auto &[current_tag, current_gnos] : current_tagged_gnos) {
      data_[current_uuid][current_tag] += current_gnos;
    }
  }
  return *this;
}

bool operator==(const gtid_set &first,
                const gtid_set &second) noexcept = default;

std::ostream &operator<<(std::ostream &output, const gtid_set &obj) {
  const auto gno_container_printer{
      [](std::ostream &stream, const gtid_set::gno_container &gnos) {
        for (const auto &interval : gnos) {
          const auto lower = boost::icl::lower(interval);
          const auto upper = boost::icl::upper(interval);
          stream << gtid::gno_separator << lower;
          if (upper != lower) {
            stream << gtid_set::interval_separator << upper;
          }
        }
      }};

  bool first_uuid{true};
  for (const auto &[current_uuid, current_tagged_gnos] : obj.data_) {
    for (const auto &[current_tag, current_gnos] : current_tagged_gnos) {
      if (!first_uuid) {
        output << gtid_set::uuid_separator << output.fill();
      } else {
        first_uuid = false;
      }
      output << current_uuid;
      if (!current_tag.is_empty()) {
        output << gtid::tag_separator << current_tag;
      }
      gno_container_printer(output, current_gnos);
    }
  }
  return output;
}

} // namespace binsrv::gtids
