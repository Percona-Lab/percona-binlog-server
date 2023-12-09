#include "binsrv/event/event.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "binsrv/event/checksum_algorithm_type.hpp"
#include "binsrv/event/code_type.hpp"
#include "binsrv/event/flag_type.hpp"
#include "binsrv/event/generic_body.hpp"
#include "binsrv/event/generic_post_header.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

event::event(util::const_byte_span portion,
             const optional_format_description_post_header &fde_post_header,
             const optional_format_description_body &fde_body)
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

  const auto type_code = common_header_.get_type_code();

  std::size_t footer_size{0U};
  if (type_code == code_type::format_description) {
    // format_description_events always include event footers with checksums
    footer_size = footer::size_in_bytes;
  } else {
    if (fde_body) {
      // if format_description event has already been encountered, we determine
      // whether there is a footer in the event from it
      footer_size =
          (fde_body->get_checksum_algorithm() == checksum_algorithm_type::crc32
               ? footer::size_in_bytes
               : 0U);
    } else {
      // we get in this branch only for the very first artificial rotate event
      // and in this case it does not include the footer
      footer_size = 0U;
    }
  }

  const std::size_t event_size = std::size(portion);
  if (event_size != common_header_.get_event_size_raw()) {
    util::exception_location().raise<std::invalid_argument>(
        "actual event size does not match the one specified in event common "
        "header");
  }
  std::size_t post_header_size{0U};
  if (fde_post_header) {
    // if format_description event has already been encountered in the stream,
    // we take post-header length from it
    post_header_size = fde_post_header->get_post_header_length(type_code);
  } else {
    // we expect that we can receive only 2 events before there is a
    // format_description event we can refer to: rotate (with artificial
    // flag) and format description event itself
    switch (type_code) {
    case code_type::rotate:
      if (!common_header_.get_flags().has_element(flag_type::artificial)) {
        util::exception_location().raise<std::logic_error>(
            "rotate event without preceding format_description event must have "
            "'artificial' flag set");
      }
      post_header_size = generic_post_header<code_type::rotate>::size_in_bytes;
      break;
    case code_type::format_description:
      post_header_size =
          generic_post_header<code_type::format_description>::size_in_bytes;
      break;
    default:
      util::exception_location().raise<std::logic_error>(
          "this type of event must be preceded by a format_description event");
    }
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
  emplace_post_header(type_code, post_header_portion);

  const auto body_portion = portion.subspan(
      common_header::size_in_bytes + post_header_size, body_size);
  emplace_body(type_code, body_portion);

  if (footer_size != 0U) {
    const auto footer_portion = portion.subspan(
        common_header::size_in_bytes + post_header_size + body_size,
        footer_size);
    footer_.emplace(footer_portion);
  };
}

void event::emplace_post_header(code_type code, util::const_byte_span portion) {
  // our goal here is to initialize (emplace) a specific class inside
  // 'post_header_' variant (determined via runtime argument 'code') with the
  // 'portion' byte range

  // we start with defining an alias for a member function pointer that
  // accepts a byte range and performs some modification on an object of this
  // class (on 'post_header_' member to be precise)
  using emplace_function = void (event::*)(util::const_byte_span);
  // then, we define an alias for a container that can store
  // '<number_of_events>' such member function pointers
  using emplace_function_container =
      std::array<emplace_function, number_of_events>;
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
  //     generic_post_header<number_of_events - 1U = heartbeat_log_v2>>
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
  (this->*emplace_functions[function_index])(portion);
}

void event::emplace_body(code_type code, util::const_byte_span portion) {
  // here we use the same technique as in 'emplace_post_header()'
  using emplace_function = void (event::*)(util::const_byte_span);
  using emplace_function_container =
      std::array<emplace_function, number_of_events>;
  static constexpr emplace_function_container emplace_functions{
      []<std::size_t... IndexPack>(
          std::index_sequence<IndexPack...>) -> emplace_function_container {
        return {&event::generic_emplace_body<
            generic_body<util::index_to_enum<code_type>(IndexPack)>>...};
      }(code_index_sequence{})};

  const auto function_index = util::enum_to_index(code);
  assert(function_index < util::enum_to_index(code_type::delimiter));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  (this->*emplace_functions[function_index])(portion);
}

} // namespace binsrv::event