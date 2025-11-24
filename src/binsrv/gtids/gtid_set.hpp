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

#ifndef BINSRV_GTIDS_GTID_SET_HPP
#define BINSRV_GTIDS_GTID_SET_HPP

#include "binsrv/gtids/gtid_set_fwd.hpp" // IWYU pragma: export

#include <map>

#include <boost/icl/interval_set.hpp>

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid_fwd.hpp"
#include "binsrv/gtids/tag_fwd.hpp"

namespace binsrv::gtids {

class gtid_set {
public:
  static constexpr char uuid_separator{','};
  static constexpr char interval_separator{'-'};

  gtid_set();

  // deliberately implicit
  // NOLINTNEXTLINE(hicpp-explicit-conversions)
  gtid_set(const gtid &value) { *this += value; }

  gtid_set(const gtid_set &other);
  gtid_set(gtid_set &&other) noexcept;
  gtid_set &operator=(const gtid_set &other);
  gtid_set &operator=(gtid_set &&other) noexcept;

  ~gtid_set();

  [[nodiscard]] bool is_empty() const noexcept { return data_.empty(); }

  [[nodiscard]] bool contains(const gtid &value) const noexcept;

  gtid_set &operator+=(const gtid &value);
  gtid_set &operator+=(const gtid_set &values);

  void clear() noexcept { data_.clear(); }

  friend bool operator==(const gtid_set &first,
                         const gtid_set &second) noexcept;

  friend std::ostream &operator<<(std::ostream &output, const gtid_set &obj);

private:
  using gno_container = boost::icl::interval_set<gno_t>;
  using gnos_by_tag_container = std::map<tag, gno_container>;
  using tagged_gnos_by_uid_container = std::map<uuid, gnos_by_tag_container>;

  tagged_gnos_by_uid_container data_{};
};

inline gtid_set operator+(const gtid_set &first, const gtid_set &second) {
  gtid_set result{first};
  return result += second;
}

} // namespace binsrv::gtids

#endif // BINSRV_GTIDS_GTID_SET_HPP
