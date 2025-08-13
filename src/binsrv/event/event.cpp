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

#include "binsrv/event/event.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "binsrv/event/checksum_algorithm_type.hpp"
#include "binsrv/event/code_type.hpp"
#include "binsrv/event/generic_body.hpp"
#include "binsrv/event/generic_post_header.hpp"
#include "binsrv/event/protocol_traits_fwd.hpp"
#include "binsrv/event/reader_context.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

event::event(reader_context &context, util::const_byte_span portion)
    : common_header_{
          [](util::const_byte_span event_portion) -> util::const_byte_span {
            if (std::size(event_portion) <
                decltype(common_header_)::size_in_bytes) {
              util::exception_location().raise<std::invalid_argument>(
                  "not enough data for event common header");
            }
            return event_portion.subspan(
                0, decltype(common_header_)::size_in_bytes);
          }(portion)} {
  // TODO: rework with direct member initialization

  const auto code = common_header_.get_type_code();

  std::size_t footer_size{0U};
  if (code == code_type::format_description) {
    // format_description_events always include event footers with checksums
    footer_size = footer::size_in_bytes;
  } else {
    // we determine whether there is a footer in the event from the
    // reader_context
    footer_size = (context.get_current_checksum_algorithm() ==
                           checksum_algorithm_type::crc32
                       ? footer::size_in_bytes
                       : 0U);
  }

  const std::size_t event_size = std::size(portion);
  if (event_size != common_header_.get_event_size_raw()) {
    util::exception_location().raise<std::invalid_argument>(
        "actual event size does not match the one specified in event common "
        "header");
  }

  const auto encoded_server_version{
      context.get_current_encoded_server_version()};

  std::size_t post_header_size{0U};
  post_header_size = context.get_current_post_header_length(code);
  if (post_header_size == unspecified_post_header_length) {
    util::exception_location().raise<std::invalid_argument>(
        "received event of type " + std::to_string(util::enum_to_index(code)) +
        " \"" + std::string{to_string_view(code)} +
        "\" "
        "is not known in server version " +
        std::to_string(encoded_server_version));
  }

  const std::size_t group_size =
      common_header::size_in_bytes + post_header_size + footer_size;
  if (event_size < group_size) {
    util::exception_location().raise<std::logic_error>(
        "not enough data for event post-header + body + footer");
  }
  const std::size_t body_size = event_size - group_size;

  const auto post_header_portion =
      portion.subspan(common_header::size_in_bytes, post_header_size);
  emplace_post_header(encoded_server_version, code, post_header_portion);

  const auto body_portion = portion.subspan(
      common_header::size_in_bytes + post_header_size, body_size);
  emplace_body(encoded_server_version, code, body_portion);

  if (footer_size != 0U) {
    const auto footer_portion = portion.subspan(
        common_header::size_in_bytes + post_header_size + body_size,
        footer_size);
    footer_.emplace(footer_portion);
  };
  context.process_event(*this);
}

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

} // namespace binsrv::event
