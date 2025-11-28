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

#ifndef BINSRV_GTIDS_GTID_HPP
#define BINSRV_GTIDS_GTID_HPP

#include "binsrv/gtids/gtid_fwd.hpp" // IWYU pragma: export

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/tag.hpp"
#include "binsrv/gtids/uuid.hpp"

namespace binsrv::gtids {

class gtid {
public:
  static constexpr char tag_separator{':'};
  static constexpr char gno_separator{':'};

  gtid() = default;

  // NOLINTNEXTLINE(modernize-pass-by-value)
  gtid(const uuid &uuid_component, const tag &tag_component,
       gno_t gno_component)
      : uuid_{uuid_component}, tag_{tag_component}, gno_{gno_component} {
    validate_components(uuid_, tag_, gno_);
  }

  gtid(const uuid &uuid_component, gno_t gno_component)
      : uuid_{uuid_component}, tag_{}, gno_{gno_component} {
    validate_components(uuid_, tag_, gno_);
  }

  [[nodiscard]] bool is_empty() const noexcept { return gno_ == 0ULL; }

  [[nodiscard]] const uuid &get_uuid() const noexcept { return uuid_; }

  [[nodiscard]] bool has_tag() const noexcept { return !tag_.is_empty(); }
  [[nodiscard]] const tag &get_tag() const noexcept { return tag_; }

  [[nodiscard]] gno_t get_gno() const noexcept { return gno_; }

  [[nodiscard]] friend bool operator==(const gtid &first,
                                       const gtid &second) noexcept = default;

  static void validate_components(const uuid &uuid_component,
                                  const tag &tag_component,
                                  gno_t gno_component);

private:
  uuid uuid_{};
  tag tag_{};
  gno_t gno_{};
};

} // namespace binsrv::gtids

#endif // BINSRV_GTIDS_GTID_HPP
