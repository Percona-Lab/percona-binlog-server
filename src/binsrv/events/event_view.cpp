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

#include "binsrv/events/event_view.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>

// needed for 'event_storage'
#include <boost/container/small_vector.hpp> // IWYU pragma: keep

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_header_view.hpp"
#include "binsrv/events/footer_view.hpp"
// needed for extracting info from the FDE body
#include "binsrv/events/format_description_body_impl.hpp" // IWYU pragma: keep
#include "binsrv/events/generic_body.hpp"
#include "binsrv/events/protocol_traits_fwd.hpp"
#include "binsrv/events/reader_context.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/conversion_helpers.hpp"
#include "util/crc_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::events {

event_view_base::event_view_base(const reader_context &context,
                                 util::byte_span portion)
    : portion_{portion} {
  if (get_total_size() < common_header_view_base::size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "not enough data for event common header");
  }
  const auto common_header_v{get_common_header_view()};

  const auto code = common_header_v.get_type_code();

  if (code == code_type::format_description) {
    // format_description events always include event footers with checksums
    // even if their bodies contain 'checksum_algorithm' set to 'none'
    footer_size_ = footer_view_base::size_in_bytes;
  } else {
    // we determine whether there is a footer in the event from the
    // reader_context
    footer_size_ =
        (context.is_footer_expected() ? footer_view_base::size_in_bytes : 0U);
  }

  if (get_total_size() != common_header_v.get_event_size_raw()) {
    util::exception_location().raise<std::invalid_argument>(
        "actual event size does not match the one specified in event common "
        "header");
  }

  if (code == code_type::format_description) {
    // special case for FORMAT_DESCRIPTION event

    // in a scenario when 8.0 MySQL Server was upgraded to 8.4, it is possible
    // for us to receive both 8.0 FDEs (coming from binlog files created before
    // the upgrade) and 8.4 FDEs (coming from binlog files created after the
    // upgrade)

    // as 8.0 and 8.4 FDEs have different sizes of their post header section,
    // the standard mechanism of extracting expected post header length from
    // the context does not work here

    // instead, we rely on the fact that FDE have body of a fixed size (1 byte
    // for 'checksum_algorithm') and we can calculate the size of the
    // post header based on that
    const std::size_t group_size =
        get_common_header_size() +
        generic_body<code_type::format_description>::size_in_bytes +
        get_footer_size();
    if (get_total_size() < group_size) {
      util::exception_location().raise<std::logic_error>(
          "not enough data for format description event common header + body + "
          "footer");
    }
    post_header_size_ = get_total_size() - group_size;
  } else {
    // for every other event, we proceed the standard way
    post_header_size_ = context.get_current_post_header_length(code);
    if (post_header_size_ == unspecified_post_header_length) {
      util::exception_location().raise<std::invalid_argument>(
          "received event of type " +
          std::to_string(util::enum_to_index(code)) + " \"" +
          std::string{to_string_view(code)} +
          "\" "
          "is not known in server version " +
          std::to_string(context.get_current_encoded_server_version()));
    }

    const std::size_t group_size =
        get_common_header_size() + get_post_header_size() + get_footer_size();
    if (get_total_size() < group_size) {
      util::exception_location().raise<std::logic_error>(
          "not enough data for event common header + post header + footer");
    }
  }

  // optional checksum verification
  if (!context.is_checksum_verification_enabled()) {
    return;
  }
  if (!has_footer()) {
    return;
  }

  if (code == code_type::format_description) {
    // FORMAT_DESCRIPTION events should be handled in a special way: whether
    // we should verify checksum for them or not is determined by the value
    // of the 'checksum_algorithm' in their bodies, not by the reader_context
    // (as for all other events)
    const generic_body<code_type::format_description> body{get_body_raw()};
    if (!body.has_checksum_algorithm()) {
      return;
    }
  }

  const auto footer_v{get_footer_view()};
  if (calculate_crc() != footer_v.get_crc_raw()) {
    util::exception_location().raise<std::invalid_argument>(
        "event checksum mismatch");
  }
}

[[nodiscard]] std::uint32_t event_view_base::calculate_crc() const noexcept {
  return util::calculate_crc32(
      get_portion().subspan(0U, get_total_size() - get_footer_size()));
}

[[nodiscard]] common_header_view
event_view_base::get_common_header_view() const {
  return common_header_view{get_common_header_raw()};
}

[[nodiscard]] common_header_updatable_view
event_view_base::get_common_header_updatable_view() const {
  return common_header_updatable_view{get_common_header_updatable_raw()};
}

[[nodiscard]] footer_view event_view_base::get_footer_view() const {
  return footer_view{get_footer_raw()};
}

[[nodiscard]] footer_updatable_view
event_view_base::get_footer_updatable_view() const {
  return footer_updatable_view{get_footer_updatable_raw()};
}

std::ostream &operator<<(std::ostream &output, const event_view &obj) {
  return output << "common header: " << event_view::get_common_header_size()
                << " byte(s), post header: " << obj.get_post_header_size()
                << " byte(s), body: " << obj.get_body_size()
                << " byte(s), footer: " << obj.get_footer_size() << " byte(s)";
}

} // namespace binsrv::events
