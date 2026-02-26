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

#include "binsrv/events/event.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <utility>
#include <variant>

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/event_view.hpp"
#include "binsrv/events/generic_body.hpp"
#include "binsrv/events/generic_post_header.hpp"
#include "binsrv/events/protocol_traits_fwd.hpp"
#include "binsrv/events/reader_context.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/conversion_helpers.hpp"

namespace binsrv::events {

event::event(reader_context &context, const event_view &view)
    : common_header_{view.get_common_header_raw()} {
  const auto encoded_server_version{
      context.get_current_encoded_server_version()};
  const auto code{common_header_.get_type_code()};

  emplace_post_header(encoded_server_version, code, view.get_post_header_raw());
  emplace_body(encoded_server_version, code, view.get_body_raw());

  if (view.has_footer()) {
    footer_.emplace(view.get_footer_view());
  };
  context.process_event(*this);
}

event::event(reader_context &context, util::const_byte_span portion)
    : event{context, event_view{context, portion}} {}

void event::emplace_post_header(std::uint32_t encoded_server_version,
                                code_type code, util::const_byte_span portion) {
  // our goal here is to initialize (emplace) a specific class inside
  // 'post_header_' variant (determined via runtime argument 'code') with the
  // 'portion' byte range

  // we start with defining an alias for a member function pointer that
  // accepts a byte range and performs some modification on an object of this
  // class (on 'post_header_' member to be precise)
  using emplace_function =
      void (event::*)(std::uint32_t, util::const_byte_span);
  // then, we define an alias for a container that can store
  // '<number_of_events>' such member function pointers
  using emplace_function_container =
      std::array<emplace_function, max_number_of_events>;
  // after that we declare a constexpr instance of such array and initialize it
  // with immediately invoked lambda
  static constexpr emplace_function_container emplace_functions{
      []<std::size_t... IndexPack>(
          std::index_sequence<IndexPack...>) -> emplace_function_container {
        return {&event::generic_emplace_post_header<
            generic_post_header<util::index_to_enum<code_type>(IndexPack)>>...};
      }(code_index_sequence{})};
  // basically, using 'IndexPack' template parameter expansion this lambda
  // will return the following array member function pointers:
  // {
  //   &event::generic_emplace_post_header<
  //     generic_post_header<0 = unknown>>,
  //   &event::generic_emplace_post_header<
  //     generic_post_header<1 = start_v3>>,
  //   ...
  //   &event::generic_emplace_post_header<
  //     generic_post_header<number_of_events - 1U = gtid_tagged_log>>
  // }

  // please, notice that we managed to avoid code bloat here by reusing the
  // same member function pointer for event codes that have identical
  // post header specializations (in this array most of the elements will
  // have the '&event::generic_emplace_post_header<unknown_post_header>'
  // value)

  // here, we reinterpret the event code as an index in our array
  const auto function_index = util::enum_to_index(code);
  assert(function_index < util::enum_to_index(code_type::delimiter));
  // all we have to do now is to call a member function pointer determined by
  // the 'function_index' offset in the array on 'this'
  // this will initialize the 'post_header_' member with expected variant

  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  (this->*emplace_functions[function_index])(encoded_server_version, portion);
}

void event::emplace_body(std::uint32_t encoded_server_version, code_type code,
                         util::const_byte_span portion) {
  // here we use the same technique as in 'emplace_post_header()'
  using emplace_function =
      void (event::*)(std::uint32_t, util::const_byte_span);
  using emplace_function_container =
      std::array<emplace_function, max_number_of_events>;
  static constexpr emplace_function_container emplace_functions{
      []<std::size_t... IndexPack>(
          std::index_sequence<IndexPack...>) -> emplace_function_container {
        return {&event::generic_emplace_body<
            generic_body<util::index_to_enum<code_type>(IndexPack)>>...};
      }(code_index_sequence{})};

  const auto function_index = util::enum_to_index(code);
  assert(function_index < util::enum_to_index(code_type::delimiter));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  (this->*emplace_functions[function_index])(encoded_server_version, portion);
}

std::ostream &operator<<(std::ostream &output, const event &obj) {
  output << "| common header | " << obj.get_common_header() << " |";

  auto generic_printer{
      [&output](const auto &alternative) { output << alternative; }};
  output << "\n| post header   | ";
  std::visit(generic_printer, obj.get_generic_post_header());

  output << " |\n| body          | ";
  std::visit(generic_printer, obj.get_generic_body());
  output << " |";
  const auto &footer{obj.get_footer()};
  if (footer) {
    output << "\n| footer        | " << *footer << " |";
  }
  return output;
}

} // namespace binsrv::events
