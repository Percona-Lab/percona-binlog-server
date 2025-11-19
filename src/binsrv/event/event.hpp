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

#ifndef BINSRV_EVENT_EVENT_HPP
#define BINSRV_EVENT_EVENT_HPP

#include "binsrv/event/event_fwd.hpp" // IWYU pragma: export

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

#include "binsrv/event/anonymous_gtid_log_body_impl.hpp" // IWYU pragma: export
#include "binsrv/event/anonymous_gtid_log_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/event/code_type.hpp"
#include "binsrv/event/common_header.hpp"                // IWYU pragma: export
#include "binsrv/event/footer.hpp"                       // IWYU pragma: export
#include "binsrv/event/format_description_body_impl.hpp" // IWYU pragma: export
#include "binsrv/event/format_description_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/event/generic_body.hpp"              // IWYU pragma: export
#include "binsrv/event/generic_post_header.hpp"       // IWYU pragma: export
#include "binsrv/event/gtid_log_body_impl.hpp"        // IWYU pragma: export
#include "binsrv/event/gtid_log_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/event/gtid_tagged_log_body_impl.hpp" // IWYU pragma: export
#include "binsrv/event/gtid_tagged_log_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/event/reader_context_fwd.hpp"
#include "binsrv/event/rotate_body_impl.hpp"        // IWYU pragma: export
#include "binsrv/event/rotate_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/event/stop_body_impl.hpp"          // IWYU pragma: export
#include "binsrv/event/stop_post_header_impl.hpp"   // IWYU pragma: export
#include "binsrv/event/unknown_body.hpp"            // IWYU pragma: export
#include "binsrv/event/unknown_post_header.hpp"     // IWYU pragma: export

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

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

  event(reader_context &context, util::const_byte_span portion);

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

  [[nodiscard]] const optional_footer &get_footer() const noexcept {
    return footer_;
  }

private:
  common_header common_header_;
  post_header_variant post_header_;
  body_variant body_;
  optional_footer footer_;

  template <typename T>
  void generic_emplace_post_header(std::uint32_t encoded_server_version,
                                   util::const_byte_span portion) {
    if constexpr (std::is_constructible_v<T, util::const_byte_span>) {
      post_header_.emplace<T>(portion);
    } else {
      post_header_.emplace<T>(encoded_server_version, portion);
    }
  }
  void emplace_post_header(std::uint32_t encoded_server_version, code_type code,
                           util::const_byte_span portion);
  template <typename T>
  void generic_emplace_body(std::uint32_t encoded_server_version,
                            util::const_byte_span portion) {
    if constexpr (std::is_constructible_v<T, util::const_byte_span>) {
      body_.emplace<T>(portion);
    } else {
      body_.emplace<T>(encoded_server_version, portion);
    }
  }
  void emplace_body(std::uint32_t encoded_server_version, code_type code,
                    util::const_byte_span portion);
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_EVENT_HPP
