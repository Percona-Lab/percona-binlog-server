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

#ifndef BINSRV_EVENTS_GTID_TAGGED_LOG_BODY_IMPL_HPP
#define BINSRV_EVENTS_GTID_TAGGED_LOG_BODY_IMPL_HPP

#include "binsrv/events/gtid_tagged_log_body_impl_fwd.hpp" // IWYU pragma: export

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "binsrv/events/gtid_log_flag_type_fwd.hpp"

#include "binsrv/gtids/common_types.hpp"
#include "binsrv/gtids/gtid_fwd.hpp"
#include "binsrv/gtids/tag_fwd.hpp"
#include "binsrv/gtids/uuid_fwd.hpp"

#include "util/bounded_string_storage.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/semantic_version_fwd.hpp"

namespace binsrv::events {

template <> class [[nodiscard]] generic_body_impl<code_type::gtid_tagged_log> {
public:
  // https://github.com/mysql/mysql-server/blob/mysql-8.4.6/libs/mysql/binlog/event/control_events.h#L1111

  explicit generic_body_impl(util::const_byte_span portion);

  [[nodiscard]] std::uint8_t get_flags_raw() const noexcept { return flags_; }
  [[nodiscard]] gtid_log_flag_set get_flags() const noexcept;
  [[nodiscard]] std::string get_readable_flags() const;

  [[nodiscard]] const gtids::uuid_storage &get_uuid_raw() const noexcept {
    return uuid_;
  }
  [[nodiscard]] gtids::uuid get_uuid() const noexcept;
  [[nodiscard]] std::string get_readable_uuid() const;

  [[nodiscard]] std::int64_t get_gno_raw() const noexcept { return gno_; }
  [[nodiscard]] gtids::gno_t get_gno() const noexcept {
    return static_cast<gtids::gno_t>(get_gno_raw());
  }

  [[nodiscard]] const gtids::tag_storage &get_tag_raw() const noexcept {
    return tag_;
  }
  [[nodiscard]] gtids::tag get_tag() const;
  [[nodiscard]] std::string_view get_readable_tag() const noexcept;

  [[nodiscard]] gtids::gtid get_gtid() const;

  [[nodiscard]] std::int64_t get_last_committed_raw() const noexcept {
    return last_committed_;
  }

  [[nodiscard]] std::int64_t get_sequence_number_raw() const noexcept {
    return sequence_number_;
  }

  [[nodiscard]] bool has_immediate_commit_timestamp() const noexcept {
    return immediate_commit_timestamp_ != unset_commit_timestamp;
  }
  [[nodiscard]] std::uint64_t
  get_immediate_commit_timestamp_raw() const noexcept {
    return immediate_commit_timestamp_;
  }
  [[nodiscard]] std::chrono::high_resolution_clock::time_point
  get_immediate_commit_timestamp() const noexcept {
    return std::chrono::high_resolution_clock::time_point{
        std::chrono::microseconds(get_immediate_commit_timestamp_raw())};
  }
  [[nodiscard]] std::string get_readable_immediate_commit_timestamp() const;

  [[nodiscard]] bool has_original_commit_timestamp() const noexcept {
    return has_original_commit_timestamp_;
  }
  [[nodiscard]] std::uint64_t
  get_original_commit_timestamp_raw() const noexcept {
    return original_commit_timestamp_;
  }

  [[nodiscard]] bool has_transaction_length() const noexcept {
    return transaction_length_ != unset_transaction_length;
  }
  [[nodiscard]] std::uint64_t get_transaction_length_raw() const noexcept {
    return transaction_length_;
  }

  [[nodiscard]] bool has_original_server_version() const noexcept {
    return has_original_server_version_;
  }
  [[nodiscard]] std::uint32_t get_original_server_version_raw() const noexcept {
    return original_server_version_;
  }
  [[nodiscard]] util::semantic_version
  get_original_server_version() const noexcept;
  [[nodiscard]] std::string get_readable_original_server_version() const;

  [[nodiscard]] bool has_immediate_server_version() const noexcept {
    return immediate_server_version_ != unset_server_version;
  }
  [[nodiscard]] std::uint32_t
  get_immediate_server_version_raw() const noexcept {
    return immediate_server_version_;
  }
  [[nodiscard]] util::semantic_version
  get_immediate_server_version() const noexcept;
  [[nodiscard]] std::string get_readable_immediate_server_version() const;

  [[nodiscard]] bool has_commit_group_ticket() const noexcept {
    return has_commit_group_ticket_;
  }
  [[nodiscard]] std::uint64_t get_commit_group_ticket_raw() const noexcept {
    return commit_group_ticket_;
  }

  // Mutators used by the rewrite-mode GTID renumberer. They modify only
  // the in-memory representation; serializing back to bytes requires
  // calling encode_to() on a freshly sized buffer.
  void set_last_committed_raw(std::int64_t value) noexcept {
    last_committed_ = value;
  }
  void set_sequence_number_raw(std::int64_t value) noexcept {
    sequence_number_ = value;
  }
  void set_transaction_length_raw(std::uint64_t value) noexcept {
    transaction_length_ = value;
  }

  // Returns the total number of bytes that encode_to() will emit for the
  // current in-memory state, including the 3-field framing header
  // (serialization_version_number, serializable_field_size,
  // last_non_ignorable_field_id) and every TLV field. The value of the
  // serializable_field_size field is computed self-consistently.
  [[nodiscard]] std::size_t calculate_encoded_size() const;

  // Writes the body to *destination* using the same TLV layout as the
  // input. The set of optional fields actually emitted matches the set
  // observed during construction (decoding); only the values of
  // last_committed / sequence_number / transaction_length may have been
  // mutated through the corresponding setters above.
  //
  // Precondition: std::size(destination) == calculate_encoded_size().
  // The destination span size is read back as the on-wire
  // serializable_field_size, so passing a wrong size produces a
  // wrong-but-self-consistent encoding. Caller is expected to verify
  // post-encode that `destination` was fully consumed.
  void encode_to(util::byte_span &destination) const;

  friend bool operator==(const generic_body_impl & /* first */,
                         const generic_body_impl & /* second */) = default;

private:
  static constexpr std::uint64_t unset_commit_timestamp{
      std::numeric_limits<std::uint64_t>::max()};
  static constexpr std::uint64_t unset_transaction_length{
      std::numeric_limits<std::uint64_t>::max()};
  static constexpr std::uint32_t unset_server_version{
      std::numeric_limits<std::uint32_t>::max()};
  static constexpr std::uint64_t unset_commit_group_ticket{
      std::numeric_limits<std::uint64_t>::max()};

  // The protocol fields below are deliberately reordered for better
  // packing (largest first, then 32-bit, then 8-bit/bool last). The
  // trailing "// N" annotation is the protocol field_id_type value
  // upstream assigns to the field (see field_id_type enum in
  // gtid_tagged_log_body_impl.cpp / define_fields() in upstream
  // control_events.h), so the deviation from protocol order is
  // visible at a glance.
  std::int64_t gno_{};                                               // 2
  std::int64_t last_committed_{};                                    // 4
  std::int64_t sequence_number_{};                                   // 5
  std::uint64_t immediate_commit_timestamp_{unset_commit_timestamp}; // 6
  std::uint64_t original_commit_timestamp_{unset_commit_timestamp};  // 7
  std::uint64_t transaction_length_{unset_transaction_length};       // 8
  std::uint64_t commit_group_ticket_{unset_commit_group_ticket};     // 11
  gtids::uuid_storage uuid_{};                                       // 1
  gtids::tag_storage tag_{};                                         // 3
  std::uint32_t original_server_version_{unset_server_version};      // 10
  std::uint32_t immediate_server_version_{unset_server_version};     // 9
  std::uint8_t flags_{};                                             // 0

  // Echoed verbatim on encode_to() so the framing header matches the
  // original. Reading and re-emitting it preserves forward compatibility
  // with newer servers that may add ignorable fields above the current
  // last non-ignorable id.
  std::uint8_t last_non_ignorable_field_id_{0U};

  // Recorded during decoding so that encode_to() emits exactly the same
  // set of optional fields as was observed in the input event. This makes
  // re-serialization byte-stable for unchanged member values and avoids
  // having to reverse-engineer upstream's encode predicates from sentinel
  // values (which would be brittle if a real timestamp/version ever
  // happened to coincide with a sentinel).
  bool has_original_commit_timestamp_{false};
  bool has_original_server_version_{false};
  bool has_commit_group_ticket_{false};

  void process_field_data(std::uint8_t field_id,
                          util::const_byte_span &remainder);

  // Helpers for calculate_encoded_size() / encode_to().
  // Returns the size in bytes of the TLV section (every <field_id,
  // field_data> pair after the framing header).
  [[nodiscard]] std::size_t calculate_tlv_section_size() const noexcept;
  // Writes only the TLV section to *destination*; assumes destination has
  // at least calculate_tlv_section_size() bytes available.
  void encode_tlv_section_to(util::byte_span &destination) const;
};

} // namespace binsrv::events

#endif // BINSRV_EVENTS_GTID_TAGGED_LOG_BODY_IMPL_HPP
