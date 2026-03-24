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

#ifndef BINSRV_EVENTS_EVENT_HPP
#define BINSRV_EVENTS_EVENT_HPP

#include "binsrv/events/event_fwd.hpp" // IWYU pragma: export

#include <array>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <boost/mp11/algorithm.hpp>
#include <boost/mp11/integer_sequence.hpp>
#include <boost/mp11/list.hpp>

#include "binsrv/events/anonymous_gtid_log_body_impl.hpp" // IWYU pragma: export
#include "binsrv/events/anonymous_gtid_log_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_header.hpp" // IWYU pragma: export
#include "binsrv/events/common_header_flag_type.hpp"
#include "binsrv/events/event_view_fwd.hpp"
#include "binsrv/events/footer.hpp"                       // IWYU pragma: export
#include "binsrv/events/format_description_body_impl.hpp" // IWYU pragma: export
#include "binsrv/events/format_description_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/events/generic_body.hpp"              // IWYU pragma: export
#include "binsrv/events/generic_post_header.hpp"       // IWYU pragma: export
#include "binsrv/events/gtid_log_body_impl.hpp"        // IWYU pragma: export
#include "binsrv/events/gtid_log_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/events/gtid_tagged_log_body_impl.hpp" // IWYU pragma: export
#include "binsrv/events/gtid_tagged_log_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/events/previous_gtids_log_body_impl.hpp" // IWYU pragma: export
#include "binsrv/events/previous_gtids_log_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/events/reader_context_fwd.hpp"
#include "binsrv/events/rotate_body_impl.hpp"        // IWYU pragma: export
#include "binsrv/events/rotate_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/events/stop_body_impl.hpp"          // IWYU pragma: export
#include "binsrv/events/stop_post_header_impl.hpp"   // IWYU pragma: export
#include "binsrv/events/unknown_body.hpp"            // IWYU pragma: export
#include "binsrv/events/unknown_post_header.hpp"     // IWYU pragma: export

#include "util/byte_span_fwd.hpp"

namespace binsrv::events {

class [[nodiscard]] event {
private:
  // here we create an index sequence (std::index_sequence) specialized
  // with the following std::size_t constant pack: 0 .. <number_of_events>
  using code_index_sequence = std::make_index_sequence<max_number_of_events>;
  // almost all boost::mp11 algorithms accept lists of types (rather than
  // lists of constants), so we convert std::index_sequence into
  // boost::mp11::mp_list of std::integral_constant types
  using wrapped_code_index_sequence =
      boost::mp11::mp_from_sequence<code_index_sequence>;

  // the following alias template is used later as a boost::mp11
  // metafunction: it transforms an Index (represented via
  // std::integral_constant) into one of the post header specializations
  template <typename Index>
  using index_to_post_header_mf =
      generic_post_header<util::index_to_enum<code_type>(Index::value)>;
  // using the list of integral constants and the
  // "event code" -> "post header specialization" conversion metafunction,
  // we create a type list of all possible post header specializations
  // (containing duplicates)
  using post_header_type_list =
      boost::mp11::mp_transform<index_to_post_header_mf,
                                wrapped_code_index_sequence>;
  // here we remove all the duplicates from the type list above
  using unique_post_header_type_list =
      boost::mp11::mp_unique<post_header_type_list>;

  // identical technique is used to obtain the std::variant of all possible
  // unique event bodies
  template <typename Index>
  using index_to_body_mf =
      generic_body<util::index_to_enum<code_type>(Index::value)>;
  using body_type_list =
      boost::mp11::mp_transform<index_to_body_mf, wrapped_code_index_sequence>;
  using unique_body_type_list = boost::mp11::mp_unique<body_type_list>;

public:
  // here we rename the type list (with unique types) into
  // std::variant
  using post_header_variant =
      boost::mp11::mp_rename<unique_post_header_type_list, std::variant>;
  using body_variant =
      boost::mp11::mp_rename<unique_body_type_list, std::variant>;

  using optional_footer = std::optional<footer>;

  template <code_type Code>
  static event
  create_event(std::uint32_t offset, const util::ctime_timestamp &timestamp,
               std::uint32_t server_id, common_header_flag_set flags,
               const generic_post_header<Code> &post_header,
               const generic_body<Code> &body, bool include_checksum,
               event_storage &buffer) {
    return event{Code,        offset, timestamp,        server_id, flags,
                 post_header, body,   include_checksum, &buffer};
  }

  template <code_type Code>
  static event
  create_event(std::uint32_t offset, const util::ctime_timestamp &timestamp,
               std::uint32_t server_id, common_header_flag_set flags,
               const generic_post_header<Code> &post_header,
               const generic_body<Code> &body, bool include_checksum) {
    return event{Code,        offset, timestamp,        server_id, flags,
                 post_header, body,   include_checksum, nullptr};
  }

  explicit event(const event_view &view);
  event(const reader_context &context, util::const_byte_span portion);

  [[nodiscard]] const common_header &get_common_header() const noexcept {
    return common_header_;
  }
  [[nodiscard]] const post_header_variant &
  get_generic_post_header() const noexcept {
    return post_header_;
  }
  template <code_type Code> [[nodiscard]] const auto &get_post_header() const {
    return std::get<generic_post_header<Code>>(get_generic_post_header());
  }

  [[nodiscard]] const body_variant &get_generic_body() const noexcept {
    return body_;
  }
  template <code_type Code> [[nodiscard]] const auto &get_body() const {
    return std::get<generic_body<Code>>(get_generic_body());
  }

  [[nodiscard]] const optional_footer &get_footer() const { return footer_; }

  [[nodiscard]] std::size_t calculate_encoded_size() const;
  void encode_to(util::byte_span &destination) const;

  friend bool operator==(const event & /* first */,
                         const event & /* second */) = default;

private:
  common_header common_header_;
  post_header_variant post_header_{};
  body_variant body_{};
  optional_footer footer_{};

  template <typename PostHeaderType, typename BodyType>
  event(code_type code, std::uint32_t offset,
        const util::ctime_timestamp &timestamp, std::uint32_t server_id,
        common_header_flag_set flags, const PostHeaderType &post_header,
        const BodyType &body, bool include_checksum, event_storage *buffer)
      : common_header_{common_header::create_with_offset(
            offset,
            static_cast<std::uint32_t>(
                common_header::calculate_encoded_size() +
                post_header.calculate_encoded_size() +
                body.calculate_encoded_size() +
                (include_checksum ? footer::calculate_encoded_size() : 0U)),
            timestamp, code, server_id, flags)},
        post_header_{post_header}, body_{body}, footer_{} {
    // format description events must always include footer
    if (code == code_type::format_description) {
      include_checksum = true;
    }
    if (buffer != nullptr) {
      encode_and_checksum(*buffer, include_checksum);
    } else {
      if (include_checksum) {
        event_storage local_buffer;
        encode_and_checksum(local_buffer, true);
      }
    }
  }

  template <typename T>
  void generic_emplace_post_header(util::const_byte_span portion) {
    post_header_.emplace<T>(portion);
  }
  void emplace_post_header(code_type code, util::const_byte_span portion);
  template <typename T>
  void generic_emplace_body(util::const_byte_span portion) {
    body_.emplace<T>(portion);
  }
  void emplace_body(code_type code, util::const_byte_span portion);
  void encode_and_checksum(event_storage &buffer, bool include_checksum);
};

} // namespace binsrv::events

#endif // BINSRV_EVENTS_EVENT_HPP
