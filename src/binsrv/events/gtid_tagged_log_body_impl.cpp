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

#include "binsrv/events/gtid_tagged_log_body_impl.hpp"

#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <limits>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/lexical_cast.hpp>

#include <boost/align/align_up.hpp>

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_types.hpp"
#include "binsrv/events/gtid_log_flag_type.hpp"

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid.hpp"
#include "binsrv/gtids/tag.hpp"
#include "binsrv/gtids/uuid.hpp"

#include "util/bounded_string_storage.hpp"
#include "util/byte_span.hpp"
#include "util/byte_span_extractors.hpp"
#include "util/byte_span_inserters.hpp"
#include "util/common_optional_types.hpp"
#include "util/conversion_helpers.hpp"
#include "util/exception_location_helpers.hpp"
#include "util/flag_set.hpp"
#include "util/semantic_version.hpp"
#include "util/timestamp_helpers.hpp"

namespace binsrv::events {

namespace {

// Field identifiers in the order they are emitted by upstream's
// define_fields() (libs/mysql/binlog/event/control_events.h:1111).
// Both the decoder (process_field_data) and the encoder (encode_to /
// calculate_encoded_size) use these values; keeping a single definition
// avoids drift.
enum class field_id_type : std::uint8_t {
  flags,
  uuid,
  gno,
  tag,
  last_committed,
  sequence_number,
  immediate_commit_timestamp,
  original_commit_timestamp,
  transaction_length,
  immediate_server_version,
  original_server_version,
  commit_group_ticket,

  delimiter
};

// matches upstream serialization_format_version: ALWAYS 1 (see
// libs/mysql/serialization/readme.md and Gtid_event::Decoder_type)
constexpr std::uint8_t known_serialization_version_number{1U};

// GTID_TAGGED_LOG events ALWAYS have last_non_ignorable_field_id in their
// TLV serialization header set to 0 - this is why we do not store it inside
// the class members and declare it as a constant for both encoding and
// decoding
constexpr std::uint8_t known_last_non_ignorable_field_id{0U};

} // namespace

generic_body_impl<code_type::gtid_tagged_log>::generic_body_impl(
    const gtid_log_flag_set &flags, const gtids::uuid &uuid, gtids::gno_t gno,
    const gtids::tag &tag,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    events::seq_no_t last_committed, events::seq_no_t sequence_number,
    const util::high_resolution_time_point &immediate_commit_timestamp,
    const util::optional_high_resolution_time_point &original_commit_timestamp,
    std::uint64_t transaction_length,
    const util::semantic_version &immediate_server_version,
    const util::optional_semantic_version &original_server_version,
    const util::optional_uint64_t &commit_group_ticket)
    : gno_{static_cast<std::int64_t>(gno)},
      last_committed_{static_cast<std::int64_t>(last_committed)},
      sequence_number_{static_cast<std::int64_t>(sequence_number)},
      immediate_commit_timestamp_{
          util::high_resolution_time_point_to_microseconds(
              immediate_commit_timestamp)},
      original_commit_timestamp_{
          original_commit_timestamp
              ? util::high_resolution_time_point_to_microseconds(
                    *original_commit_timestamp)
              : unset_commit_timestamp},
      transaction_length_{transaction_length},
      immediate_server_version_{immediate_server_version.get_encoded()},
      original_server_version_{original_server_version
                                   ? original_server_version->get_encoded()
                                   : unset_server_version},
      commit_group_ticket_{commit_group_ticket ? *commit_group_ticket
                                               : unset_commit_group_ticket},
      uuid_{uuid.get_raw()}, tag_{tag.get_raw()}, flags_{flags.get_bits()} {}

generic_body_impl<code_type::gtid_tagged_log>::generic_body_impl(
    util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  // make sure we did OK with data members reordering
  // (summands listed in the same order as the declarations in the .hpp)
  static_assert(sizeof *this ==
                    boost::alignment::align_up(
                        sizeof gno_ + sizeof last_committed_ +
                            sizeof sequence_number_ +
                            sizeof immediate_commit_timestamp_ +
                            sizeof original_commit_timestamp_ +
                            sizeof transaction_length_ +
                            sizeof immediate_server_version_ +
                            sizeof original_server_version_ +
                            sizeof commit_group_ticket_ + sizeof uuid_ +
                            sizeof tag_ + sizeof flags_,
                        alignof(decltype(*this))),
                "inefficient data member reordering in gtid_log event body");

  auto remainder = portion;
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/serialization/readme.md?plain=1#L102
  // <message_field> ::= <serialization_version_number> <message_format>
  // Extracting <serialization_version_number>

  std::uint8_t serialization_version_number{};
  if (!util::extract_varlen_int_from_byte_span_checked(
          remainder, serialization_version_number)) {
    util::exception_location().raise<std::invalid_argument>(
        "gtid_tagged_log event body is too short to extract "
        "serialization_version_number");
  }
  if (serialization_version_number != known_serialization_version_number) {
    util::exception_location().raise<std::invalid_argument>(
        "unexpected serialization_version_number in the gtid_tagged_log event "
        "body");
  }

  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/serialization/readme.md?plain=1#L113
  // <message_format> ::= <serializable_field_size>
  // <last_non_ignorable_field_id> { <type_field> }
  // Extracting <serializable_field_size>
  std::size_t serializable_field_size{};
  if (!util::extract_varlen_int_from_byte_span_checked(
          remainder, serializable_field_size)) {
    util::exception_location().raise<std::invalid_argument>(
        "gtid_tagged_log event body is too short to extract "
        "serializable_field_size");
  }

  if (serializable_field_size != std::size(portion)) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid serializable_field_size in the gtid_tagged_log event body");
  }

  // Extracting <last_non_ignorable_field_id>
  std::uint8_t last_non_ignorable_field_id{};
  if (!util::extract_varlen_int_from_byte_span_checked(
          remainder, last_non_ignorable_field_id)) {
    util::exception_location().raise<std::invalid_argument>(
        "gtid_tagged_log event body is too short to extract "
        "last_non_ignorable_field_id");
  }
  if (last_non_ignorable_field_id != known_last_non_ignorable_field_id) {
    util::exception_location().raise<std::invalid_argument>(
        "unexpected last_non_ignorable_field_id in the gtid_tagged_log event "
        "body");
  }

  // Creating a bitset for tracking which fields we have seen in the field_id
  // sequence. At the beginning we add all ids of the non-optional fields
  using field_bitset_type =
      std::bitset<util::enum_to_index(field_id_type::delimiter)>;
  field_bitset_type remaining_field_ids{};
  // setting every bit except for original_commit_timestamp,
  // original_server_version and commit_group_ticket, which are optional
  remaining_field_ids.flip();
  remaining_field_ids.reset(
      util::enum_to_index(field_id_type::original_commit_timestamp));
  remaining_field_ids.reset(
      util::enum_to_index(field_id_type::original_server_version));
  remaining_field_ids.reset(
      util::enum_to_index(field_id_type::commit_group_ticket));

  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/serialization/readme.md?plain=1#L114
  // The sequence of <type_field>
  // <type_field> ::= <field_id> <field_data>
  std::uint8_t field_id{};
  std::uint8_t last_seen_field_id{std::numeric_limits<std::uint8_t>::max()};
  while (util::extract_varlen_int_from_byte_span_checked(remainder, field_id)) {
    if (last_seen_field_id != std::numeric_limits<std::uint8_t>::max() &&
        field_id <= last_seen_field_id) {
      util::exception_location().raise<std::invalid_argument>(
          "broken field_id sequence in the gtid_tagged_log event body");
    }
    // although known_last_non_ignorable_field_id is currently 0, the logic
    // below still checks for ignorable fields rule violations
    if (field_id <= known_last_non_ignorable_field_id) {
      if (field_id != 0 && field_id != last_seen_field_id + 1U) {
        util::exception_location().raise<std::invalid_argument>(
            "violated last_non_ignorable_field_id rule in the gtid_tagged_log "
            "event body");
      }
    }
    process_field_data(field_id, remainder);
    remaining_field_ids.reset(static_cast<std::size_t>(field_id));
    last_seen_field_id = field_id;
  }

  if (remaining_field_ids.any()) {
    util::exception_location().raise<std::invalid_argument>(
        "missing required non-optional fields in the gtid_tagged_log event "
        "body");
  }
  if (!remainder.empty()) {
    util::exception_location().raise<std::invalid_argument>(
        "extra bytes in the gtid_log event body");
  }
}

[[nodiscard]] gtid_log_flag_set
generic_body_impl<code_type::gtid_tagged_log>::get_flags() const noexcept {
  return gtid_log_flag_set{get_flags_raw()};
}

[[nodiscard]] std::string
generic_body_impl<code_type::gtid_tagged_log>::get_readable_flags() const {
  return to_string(get_flags());
}

[[nodiscard]] gtids::uuid
generic_body_impl<code_type::gtid_tagged_log>::get_uuid() const noexcept {
  return gtids::uuid{get_uuid_raw()};
}

[[nodiscard]] std::string
generic_body_impl<code_type::gtid_tagged_log>::get_readable_uuid() const {
  return boost::lexical_cast<std::string>(get_uuid());
}

[[nodiscard]] gtids::tag
generic_body_impl<code_type::gtid_tagged_log>::get_tag() const {
  return gtids::tag{get_tag_raw()};
}

[[nodiscard]] std::string_view
generic_body_impl<code_type::gtid_tagged_log>::get_readable_tag()
    const noexcept {
  return util::to_string_view(get_tag_raw());
}

[[nodiscard]] gtids::gtid
generic_body_impl<code_type::gtid_tagged_log>::get_gtid() const {
  return {get_uuid(), get_tag(), get_gno()};
}

[[nodiscard]] std::string generic_body_impl<code_type::gtid_tagged_log>::
    get_readable_immediate_commit_timestamp() const {
  return util::microseconds_to_iso_extended_string(
      get_immediate_commit_timestamp_raw());
}

[[nodiscard]] std::string generic_body_impl<code_type::gtid_tagged_log>::
    get_readable_original_commit_timestamp() const {
  if (!has_original_commit_timestamp()) {
    return {};
  }
  return util::microseconds_to_iso_extended_string(
      get_original_commit_timestamp_raw());
}

[[nodiscard]] util::semantic_version
generic_body_impl<code_type::gtid_tagged_log>::get_immediate_server_version()
    const noexcept {
  return util::semantic_version{get_immediate_server_version_raw()};
}

[[nodiscard]] std::string generic_body_impl<
    code_type::gtid_tagged_log>::get_readable_immediate_server_version() const {
  return get_immediate_server_version().get_string();
}

[[nodiscard]] util::optional_semantic_version
generic_body_impl<code_type::gtid_tagged_log>::get_original_server_version()
    const noexcept {
  if (!has_original_server_version()) {
    return std::nullopt;
  }
  return util::semantic_version{get_original_server_version_raw()};
}

[[nodiscard]] std::string generic_body_impl<
    code_type::gtid_tagged_log>::get_readable_original_server_version() const {
  const auto original_server_version{get_original_server_version()};
  if (!original_server_version) {
    return {};
  }
  return original_server_version->get_string();
}

std::ostream &
operator<<(std::ostream &output,
           const generic_body_impl<code_type::gtid_tagged_log> &obj) {
  output << "flags: " << obj.get_readable_flags()
         << ", uuid: " << obj.get_readable_uuid() << ", gno: " << obj.get_gno()
         << ", tag: " << obj.get_readable_tag()
         << ", last_committed: " << obj.get_last_committed()
         << ", sequence_number: " << obj.get_sequence_number()
         << ", immediate_commit_timestamp: "
         << obj.get_readable_immediate_commit_timestamp();
  if (obj.has_original_commit_timestamp()) {
    output << ", original_commit_timestamp: "
           << obj.get_readable_original_commit_timestamp();
  }
  output << ", transaction_length: " << obj.get_transaction_length_raw()
         << ", immediate_server_version: "
         << obj.get_readable_immediate_server_version();
  if (obj.has_original_server_version()) {
    output << ", original_server_version: "
           << obj.get_readable_original_server_version();
  }
  if (obj.has_commit_group_ticket()) {
    output << ", commit_group_ticket: " << obj.get_commit_group_ticket_raw();
  }

  return output;
}

void generic_body_impl<code_type::gtid_tagged_log>::process_field_data(
    std::uint8_t field_id, util::const_byte_span &remainder) {
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.h#L1111
  const auto varlen_int_extractor{
      [](util::const_byte_span &source, auto &target, std::string_view label) {
        if (!util::extract_varlen_int_from_byte_span_checked(source, target)) {
          util::exception_location().raise<std::invalid_argument>(
              "gtid_tagged_log event body is too short to extract " +
              std::string{label});
        }
      }};

  switch (static_cast<field_id_type>(field_id)) {
  case field_id_type::flags:
    varlen_int_extractor(remainder, flags_, "flags");
    break;
  case field_id_type::uuid: {
    // Extracting a fixed-size (16 byte) array of varlen bytes
    std::uint8_t extracted_uuid_byte{};
    for (auto &uuid_byte : uuid_) {
      varlen_int_extractor(remainder, extracted_uuid_byte, "uuid");
      uuid_byte = util::from_underlying<std::byte>(extracted_uuid_byte);
    }
  } break;
  case field_id_type::gno:
    // TODO: add validation (gno >= gtid::min_gno && gno_ < gtid::max_gno)
    varlen_int_extractor(remainder, gno_, "gno");
    break;
  case field_id_type::tag: {
    // Extracting tag (length encoded as varlent int and raw character array)
    std::size_t extracted_tag_length{};
    varlen_int_extractor(remainder, extracted_tag_length, "tag length");
    tag_.resize(extracted_tag_length);
    const std::span<gtids::tag_storage::value_type> tag_subrange{tag_};
    if (!util::extract_byte_span_from_byte_span_checked(remainder,
                                                        tag_subrange)) {
      util::exception_location().raise<std::invalid_argument>(
          "gtid_tagged_log event body is too short to extract tag data");
    }
  } break;
  case field_id_type::last_committed:
    varlen_int_extractor(remainder, last_committed_, "last_committed");
    break;
  case field_id_type::sequence_number:
    varlen_int_extractor(remainder, sequence_number_, "sequence_number");
    break;
  case field_id_type::immediate_commit_timestamp:
    varlen_int_extractor(remainder, immediate_commit_timestamp_,
                         "immediate_commit_timestamp");
    break;
  case field_id_type::original_commit_timestamp:
    varlen_int_extractor(remainder, original_commit_timestamp_,
                         "original_commit_timestamp");
    break;
  case field_id_type::transaction_length:
    varlen_int_extractor(remainder, transaction_length_, "transaction_length");
    break;
  case field_id_type::immediate_server_version:
    varlen_int_extractor(remainder, immediate_server_version_,
                         "immediate_server_version");
    break;
  case field_id_type::original_server_version:
    varlen_int_extractor(remainder, original_server_version_,
                         "original_server_version");
    break;
  case field_id_type::commit_group_ticket:
    varlen_int_extractor(remainder, commit_group_ticket_,
                         "commit_group_ticket");
    break;
  default:
    util::exception_location().raise<std::invalid_argument>(
        "unknown field_id in the gtid_tagged_log event body");
  }
}

[[nodiscard]] std::size_t
generic_body_impl<code_type::gtid_tagged_log>::calculate_tlv_section_size()
    const noexcept {
  std::size_t total{0U};

  // field: flags
  total += util::calculate_varlen_int_size(
      util::to_underlying(field_id_type::flags));
  total += util::calculate_varlen_int_size(flags_);

  // field: uuid (16 separate varlen-encoded bytes, one per UUID byte)
  total +=
      util::calculate_varlen_int_size(util::to_underlying(field_id_type::uuid));
  for (auto uuid_byte : uuid_) {
    total += util::calculate_varlen_int_size(util::to_underlying(uuid_byte));
  }

  // field: gno
  total +=
      util::calculate_varlen_int_size(util::to_underlying(field_id_type::gno));
  total += util::calculate_varlen_int_size(gno_);

  // field: tag (varlen length + raw bytes)
  total +=
      util::calculate_varlen_int_size(util::to_underlying(field_id_type::tag));
  total += util::calculate_varlen_int_size(std::size(tag_));
  total += std::size(tag_);

  // field: last_committed
  total += util::calculate_varlen_int_size(
      util::to_underlying(field_id_type::last_committed));
  total += util::calculate_varlen_int_size(last_committed_);

  // field: sequence_number
  total += util::calculate_varlen_int_size(
      util::to_underlying(field_id_type::sequence_number));
  total += util::calculate_varlen_int_size(sequence_number_);

  // field: immediate_commit_timestamp
  total += util::calculate_varlen_int_size(
      util::to_underlying(field_id_type::immediate_commit_timestamp));
  total += util::calculate_varlen_int_size(immediate_commit_timestamp_);

  // field: original_commit_timestamp (optional)
  if (has_original_commit_timestamp()) {
    total += util::calculate_varlen_int_size(
        util::to_underlying(field_id_type::original_commit_timestamp));
    total += util::calculate_varlen_int_size(original_commit_timestamp_);
  }

  // field: transaction_length
  total += util::calculate_varlen_int_size(
      util::to_underlying(field_id_type::transaction_length));
  total += util::calculate_varlen_int_size(transaction_length_);

  // field: immediate_server_version
  total += util::calculate_varlen_int_size(
      util::to_underlying(field_id_type::immediate_server_version));
  total += util::calculate_varlen_int_size(immediate_server_version_);

  // field: original_server_version (optional)
  if (has_original_server_version()) {
    total += util::calculate_varlen_int_size(
        util::to_underlying(field_id_type::original_server_version));
    total += util::calculate_varlen_int_size(original_server_version_);
  }

  // field: commit_group_ticket (optional)
  if (has_commit_group_ticket()) {
    total += util::calculate_varlen_int_size(
        util::to_underlying(field_id_type::commit_group_ticket));
    total += util::calculate_varlen_int_size(commit_group_ticket_);
  }

  return total;
}

[[nodiscard]] std::size_t
generic_body_impl<code_type::gtid_tagged_log>::calculate_encoded_size() const {
  // body layout:
  //   [varlen: serialization_version_number == 1                ]
  //   [varlen: serializable_field_size (== total body size)     ]
  //   [varlen: last_non_ignorable_field_id                      ]
  //   [TLV section: calculate_tlv_section_size() bytes          ]
  //
  // serializable_field_size encodes the entire body length INCLUDING
  // itself, which makes it self-referential: the value depends on its
  // own varlen-encoded width. Solve via a tiny iteration: increasing
  // the value monotonically grows its width (1->2->3->...->9), so the
  // loop always terminates in at most 9 steps.
  const std::size_t framing_misc_size{
      util::calculate_varlen_int_size(known_serialization_version_number) +
      util::calculate_varlen_int_size(known_last_non_ignorable_field_id)};
  const std::size_t tlv_size{calculate_tlv_section_size()};
  const std::size_t tail{framing_misc_size + tlv_size};

  // initial guess: the encoded size of the total size varlen integer is
  // minimal (1 byte)
  std::size_t width{1U};
  std::size_t new_width{};
  std::size_t iteration{0U};
  static constexpr std::size_t max_number_of_iterations{9U};
  while (
      ((new_width = util::calculate_varlen_int_size(tail + width)) != width) &&
      (iteration < max_number_of_iterations)) {
    width = new_width;
    ++iteration;
  }
  assert(iteration < max_number_of_iterations);
  return tail + width;
}

void generic_body_impl<code_type::gtid_tagged_log>::encode_tlv_section_to(
    util::byte_span &destination) const {
  const auto emit_field_id{[&destination](field_id_type field_id) {
    [[maybe_unused]] auto result{util::insert_varlen_int_to_byte_span_checked(
        destination, util::to_underlying(field_id))};
  }};

  // It is OK to ignore the result here as we already ensured
  // that destination is large enough.
  [[maybe_unused]] bool insert_result{};

  // field: flags
  emit_field_id(field_id_type::flags);
  insert_result =
      util::insert_varlen_int_to_byte_span_checked(destination, flags_);

  // field: uuid (16 separate varlen-encoded bytes)
  emit_field_id(field_id_type::uuid);
  for (auto uuid_byte : uuid_) {
    insert_result = util::insert_varlen_int_to_byte_span_checked(
        destination, util::to_underlying(uuid_byte));
  }

  // field: gno
  emit_field_id(field_id_type::gno);
  insert_result =
      util::insert_varlen_int_to_byte_span_checked(destination, gno_);

  // field: tag (varlen length + raw bytes)
  emit_field_id(field_id_type::tag);
  insert_result = util::insert_varlen_int_to_byte_span_checked(destination,
                                                               std::size(tag_));
  const util::const_byte_span tag_span{tag_};
  util::insert_byte_span_to_byte_span(destination, tag_span);

  // field: last_committed
  emit_field_id(field_id_type::last_committed);
  insert_result = util::insert_varlen_int_to_byte_span_checked(destination,
                                                               last_committed_);

  // field: sequence_number
  emit_field_id(field_id_type::sequence_number);
  insert_result = util::insert_varlen_int_to_byte_span_checked(
      destination, sequence_number_);

  // field: immediate_commit_timestamp
  emit_field_id(field_id_type::immediate_commit_timestamp);
  insert_result = util::insert_varlen_int_to_byte_span_checked(
      destination, immediate_commit_timestamp_);

  // field: original_commit_timestamp (optional)
  if (has_original_commit_timestamp()) {
    emit_field_id(field_id_type::original_commit_timestamp);
    insert_result = util::insert_varlen_int_to_byte_span_checked(
        destination, original_commit_timestamp_);
  }

  // field: transaction_length
  emit_field_id(field_id_type::transaction_length);
  insert_result = util::insert_varlen_int_to_byte_span_checked(
      destination, transaction_length_);

  // field: immediate_server_version
  emit_field_id(field_id_type::immediate_server_version);
  insert_result = util::insert_varlen_int_to_byte_span_checked(
      destination, immediate_server_version_);

  // field: original_server_version (optional)
  if (has_original_server_version()) {
    emit_field_id(field_id_type::original_server_version);
    insert_result = util::insert_varlen_int_to_byte_span_checked(
        destination, original_server_version_);
  }

  // field: commit_group_ticket (optional)
  if (has_commit_group_ticket()) {
    emit_field_id(field_id_type::commit_group_ticket);
    insert_result = util::insert_varlen_int_to_byte_span_checked(
        destination, commit_group_ticket_);
  }
}

void generic_body_impl<code_type::gtid_tagged_log>::encode_to(
    util::byte_span &destination) const {
  const std::uint64_t encoded_size{calculate_encoded_size()};
  if (std::size(destination) < encoded_size) {
    util::exception_location().raise<std::invalid_argument>(
        "cannot encode gtid_tagged_log event body");
  }

  // The framing header: serialization_version_number,
  // serializable_field_size, last_non_ignorable_field_id.
  // It is OK to ignore the result here as we already ensured
  // that destination is large enough.
  [[maybe_unused]] bool insert_result{};
  insert_result = util::insert_varlen_int_to_byte_span_checked(
      destination, known_serialization_version_number);
  insert_result =
      util::insert_varlen_int_to_byte_span_checked(destination, encoded_size);
  insert_result = util::insert_varlen_int_to_byte_span_checked(
      destination, known_last_non_ignorable_field_id);

  encode_tlv_section_to(destination);
}

} // namespace binsrv::events
