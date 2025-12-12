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
#include "binsrv/gtids/uuid_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::gtids {

class gtid_set {
public:
  static constexpr char uuid_separator{','};
  static constexpr char interval_separator{'-'};

  gtid_set();

  // deliberately implicit, delegating to default constructor here
  // in order to deal with incomplete tag type
  // NOLINTNEXTLINE(hicpp-explicit-conversions)
  gtid_set(const gtid &value) : gtid_set() { *this += value; }

  explicit gtid_set(util::const_byte_span portion);

  gtid_set(const gtid_set &other);
  gtid_set(gtid_set &&other) noexcept;
  gtid_set &operator=(const gtid_set &other);
  gtid_set &operator=(gtid_set &&other) noexcept;

  ~gtid_set();

  [[nodiscard]] bool is_empty() const noexcept { return data_.empty(); }
  [[nodiscard]] bool contains_tags() const noexcept;

  [[nodiscard]] std::size_t calculate_encoded_size() const noexcept;
  void encode_to(util::byte_span &destination) const;

  [[nodiscard]] bool contains(const gtid &value) const noexcept;

  void add(const uuid &uuid_component, const tag &tag_component,
           gno_t gno_component);
  void add(const gtid &value);
  void add(const gtid_set &values);

  void add_interval(const uuid &uuid_component, const tag &tag_component,
                    gno_t gno_lower_component, gno_t gno_upper_component);

  gtid_set &operator+=(const gtid &value) {
    add(value);
    return *this;
  }
  gtid_set &operator+=(const gtid_set &values) {
    add(values);
    return *this;
  }

  void clear() noexcept;

  friend bool operator==(const gtid_set &first,
                         const gtid_set &second) noexcept;

  friend std::ostream &operator<<(std::ostream &output, const gtid_set &obj);

private:
  using gno_container = boost::icl::interval_set<gno_t>;
  using gnos_by_tag_container = std::map<tag, gno_container>;
  using tagged_gnos_by_uid_container = std::map<uuid, gnos_by_tag_container>;

  tagged_gnos_by_uid_container data_;

  void process_intervals(util::const_byte_span &remainder,
                         const uuid &current_uuid, const tag &current_tag);
};

inline gtid_set operator+(const gtid_set &first, const gtid_set &second) {
  gtid_set result{first};
  return result += second;
}

} // namespace binsrv::gtids

#endif // BINSRV_GTIDS_GTID_SET_HPP
