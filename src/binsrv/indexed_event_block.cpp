// Copyright (c) 2026 Percona and/or its affiliates.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 2.0,
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

#include "binsrv/indexed_event_block.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#include "binsrv/replication_mode_type.hpp"

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_header_view.hpp"
#include "binsrv/events/event_view.hpp"
#include "binsrv/events/generic_body.hpp"
#include "binsrv/events/gtid_log_body.hpp"
#include "binsrv/events/gtid_log_post_header.hpp"
#include "binsrv/events/gtid_tagged_log_body_impl.hpp"
#include "binsrv/events/reader_context.hpp"

#include "binsrv/gtids/gtid.hpp"
#include "binsrv/gtids/gtid_set.hpp"

#include "util/common_optional_types.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

indexed_event_block::indexed_event_block(util::dynamic_byte_buffer buffer,
                                         std::size_t max_entities_to_parse)
    : indexed_event_block(indexed_event_block_mode_type::by_event,
                          std::move(buffer), gtids::gtid_set{}, 0U, false,
                          max_entities_to_parse) {}

indexed_event_block::indexed_event_block(util::dynamic_byte_buffer buffer,
                                         const gtids::gtid_set &gtids,
                                         std::uint32_t encoded_server_version,
                                         bool checksum_enabled,
                                         std::size_t max_entities_to_parse)
    : indexed_event_block(indexed_event_block_mode_type::by_transaction,
                          std::move(buffer), gtids, encoded_server_version,
                          checksum_enabled, max_entities_to_parse) {}

indexed_event_block::indexed_event_block(indexed_event_block_mode_type mode,
                                         util::dynamic_byte_buffer buffer,
                                         const gtids::gtid_set &gtids,
                                         std::uint32_t encoded_server_version,
                                         bool checksum_enabled,
                                         std::size_t max_entities_to_parse)
    : mode_{mode}, buffer_{std::move(buffer)},
      encoded_server_version_{encoded_server_version},
      checksum_enabled_{checksum_enabled} {
  if (get_mode() == indexed_event_block_mode_type::by_event) {
    build_index_by_event(max_entities_to_parse);
  } else {
    build_index_by_transaction(gtids, max_entities_to_parse);
  }
}

[[nodiscard]] util::optional_size_t
indexed_event_block::get_first_entity_size_in_unparsed_tail() const {
  const auto offset(get_parsed_size());

  if (get_mode() == indexed_event_block_mode_type::by_event) {
    return extract_event_size_internal(offset);
  }

  assert(get_mode() == indexed_event_block_mode_type::by_transaction);

  std::size_t event_size{};
  if (!parse_single_event(offset, event_size)) {
    return {};
  }

  const binsrv::events::reader_context fake_ctx{
      encoded_server_version_, /* connection_encoded_server_version */
      checksum_enabled_,       /* checksum_verification_enabled */
      binsrv::replication_mode_type::position, /* replication_mode */
      "",                                      /* binlog_name */
      0U,                                      /* position */
  };

  events::code_type code{};
  std::size_t transaction_size{};
  parse_single_transaction_internal(fake_ctx, offset, event_size, code,
                                    transaction_size, nullptr);

  return transaction_size;
}

util::optional_size_t indexed_event_block::extract_event_size_internal(
    std::size_t offset) const noexcept {
  if (std::size(buffer_) - offset <
      events::common_header_view_base::size_in_bytes) {
    return {};
  }
  const events::common_header_view header{
      util::const_byte_span{buffer_}.subspan(
          offset, events::common_header_view_base::size_in_bytes)};
  return header.get_event_size_raw();
}

[[nodiscard]] bool
indexed_event_block::parse_single_event(std::size_t offset,
                                        std::size_t &size) const {
  assert(offset <= std::size(buffer_));

  const auto extracted_size{extract_event_size_internal(offset)};

  if (!extracted_size.has_value()) {
    return false;
  }

  if (*extracted_size < events::common_header_view_base::size_in_bytes) {
    util::exception_location().raise<std::runtime_error>(
        "in indexed_event_block constructor encountered an event with "
        "abnormal size");
  }

  if (*extracted_size > std::size(buffer_) - offset) {
    return false;
  }

  size = *extracted_size;
  return true;
}

void indexed_event_block::parse_single_transaction_internal(
    const events::reader_context &context, std::size_t event_offset,
    std::size_t event_size, events::code_type &code,
    std::size_t &transaction_size, gtids::gtid *gtid) const {
  const events::event_view event_v{
      context,
      util::const_byte_span{buffer_}.subspan(event_offset, event_size)};

  const auto extracted_code{event_v.get_common_header_view().get_type_code()};
  gtids::gtid extracted_gtid{};
  std::uint64_t extracted_transaction_size{event_size};
  switch (extracted_code) {
  case events::code_type::format_description:
  case events::code_type::previous_gtids_log:
  case events::code_type::rotate:
  case events::code_type::stop:
    break;

  case events::code_type::anonymous_gtid_log:
  case events::code_type::gtid_log: {
    // both GTID_LOG and ANONYMOUS_GTID_LOG events have the same underlying
    // structures for representing their post headers and bodies
    if (gtid != nullptr) {
      const events::gtid_log_post_header post_header{
          event_v.get_post_header_raw()};
      extracted_gtid = post_header.get_gtid();
      if (extracted_code == events::code_type::gtid_log &&
          extracted_gtid.is_empty()) {
        util::exception_location().raise<std::logic_error>(
            "in indexed_event_block encountered an empty gtid in the gtid log "
            "event");
      }
    }
    const events::gtid_log_body body{event_v.get_body_raw()};
    extracted_transaction_size = body.get_transaction_length_raw();
  } break;

  case events::code_type::gtid_tagged_log: {
    const auto body{events::generic_body<events::code_type::gtid_tagged_log>{
        event_v.get_body_raw()}};
    if (gtid != nullptr) {
      extracted_gtid = body.get_gtid();
      if (extracted_gtid.is_empty()) {
        util::exception_location().raise<std::logic_error>(
            "in indexed_event_block encountered an empty gtid in the gtid "
            "tagged "
            "log event");
      }
    }
    extracted_transaction_size = body.get_transaction_length_raw();
  } break;
  default:
    util::exception_location().raise<std::logic_error>(
        "in indexed_event_block encountered an unexpected event code");
  }
  if (!std::in_range<std::size_t>(extracted_transaction_size)) {
    util::exception_location().raise<std::logic_error>(
        "in indexed_event_block extracted transaction length is too large");
  }
  code = extracted_code;
  transaction_size = static_cast<std::size_t>(extracted_transaction_size);
  if (gtid != nullptr) {
    *gtid = extracted_gtid;
  }
}

void indexed_event_block::build_index_by_event(
    std::size_t max_entities_to_parse) {
  assert(get_mode() == indexed_event_block_mode_type::by_event);
  parsed_size_ = 0UZ;

  for (std::size_t events_parsed = 0; events_parsed < max_entities_to_parse;
       ++events_parsed) {
    std::size_t event_size{};
    if (!parse_single_event(parsed_size_, event_size)) {
      break;
    }

    // TODO: consider performing checksum validation here
    index_records_.emplace_back(parsed_size_, event_size);
    parsed_size_ += event_size;
  }
}

void indexed_event_block::build_index_by_transaction(
    const gtids::gtid_set &gtids, std::size_t max_entities_to_parse) {
  assert(get_mode() == indexed_event_block_mode_type::by_transaction);

  parsed_size_ = 0UZ;
  const binsrv::events::reader_context fake_ctx{
      encoded_server_version_, /* connection_encoded_server_version */
      checksum_enabled_,       /* checksum_verification_enabled */
      binsrv::replication_mode_type::position, /* replication_mode */
      "",                                      /* binlog_name */
      0U,                                      /* position */
  };

  for (std::size_t transactions_parsed = 0;
       transactions_parsed < max_entities_to_parse; ++transactions_parsed) {
    std::size_t event_size{};
    if (!parse_single_event(parsed_size_, event_size)) {
      break;
    }

    events::code_type code{};
    gtids::gtid gtid{};
    std::size_t transaction_size{};
    parse_single_transaction_internal(fake_ctx, parsed_size_, event_size, code,
                                      transaction_size, &gtid);

    if (transaction_size < event_size) {
      util::exception_location().raise<std::logic_error>(
          "in indexed_event_block transaction size is smaller than gtid event "
          "size");
    }

    // checking if the transaction fits within the remaining buffer
    if (transaction_size > std::size(buffer_) - parsed_size_) {
      break;
    }

    // TODO: here we can do additional optimization by skipping partially
    //       downloaded (truncated) transactions earlier without downloading
    //       all the events that belong to them, but this will change the
    //       logic of how 'indexed_event_block' works - it may skip more
    //       data that was passed to its buffer
    if (gtid.is_empty() || !gtids.contains(gtid)) {
      index_records_.push_back({parsed_size_, event_size});
      const auto transaction_end{parsed_size_ + transaction_size};
      parsed_size_ += event_size;

      while (parsed_size_ < transaction_end) {
        if (!parse_single_event(parsed_size_, event_size)) {
          util::exception_location().raise<std::logic_error>(
              "in indexed_event_block failed to parse event within "
              "transaction");
        }
        index_records_.push_back({parsed_size_, event_size});
        parsed_size_ += event_size;
      }
      // we should finish this loop exactly at the end of the transaction
      if (parsed_size_ != transaction_end) {
        util::exception_location().raise<std::logic_error>(
            "in indexed_event_block transaction ended at an unexpected "
            "position");
      }
    } else {
      // if the GTID is empty or not contained in the provided GTID set
      parsed_size_ += transaction_size;
    }
  }
}

} // namespace binsrv
