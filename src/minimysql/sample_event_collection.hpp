// Copyright (c) 2023-2026 Percona and/or its affiliates.
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

#ifndef MINIMYSQL_SAMPLE_EVENT_COLLECTION_HPP
#define MINIMYSQL_SAMPLE_EVENT_COLLECTION_HPP

#include <array>
#include <string>

namespace minimysql {

class sample_event_collection {
public:
  using event_data_type = std::string;
  static constexpr auto number_of_predefined_events{10UZ};
  using event_data_container =
      std::array<event_data_type, number_of_predefined_events>;

  sample_event_collection();

  [[nodiscard]] const event_data_container &get_events() const noexcept {
    return events_;
  }

private:
  event_data_container events_;
};

} // namespace minimysql

#endif // MINIMYSQL_SAMPLE_EVENT_COLLECTION_HPP
