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

#include "binsrv/events/rewriter.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

// needed for 'event_storage'
#include <boost/container/small_vector.hpp> // IWYU pragma: keep

#include "binsrv/events/code_type.hpp"
#include "binsrv/events/common_types.hpp"
#include "binsrv/events/event_fwd.hpp"
#include "binsrv/events/event_view.hpp"
#include "binsrv/events/gtid_log_post_header.hpp"
#include "binsrv/events/gtid_tagged_log_body_impl.hpp"
#include "binsrv/events/protocol_traits_fwd.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::events {

[[nodiscard]] event_updatable_view
rewriter::materialize(const event_view &event_v, event_storage &buffer,
                      materialization_type mode) {
  return generic_materialize(
      event_v, buffer, mode,
      [](materialization_type normalized_mode,
         const event_updatable_view::write_proxy &proxy) {
        if (normalized_mode != materialization_type::leave_checksum_as_is) {
          const auto common_header_uv{proxy.get_common_header_updatable_view()};
          const auto total_size{std::size(proxy.get_updatable_portion())};
          common_header_uv.set_event_size_raw(
              static_cast<std::uint32_t>(total_size));
        }
      });
}

[[nodiscard]] event_updatable_view rewriter::materialize_and_relocate(
    const event_view &event_v, event_storage &buffer, materialization_type mode,
    std::uint64_t offset) {
  return generic_materialize(
      event_v, buffer, mode,
      [offset](materialization_type normalized_mode,
               const event_updatable_view::write_proxy &proxy) {
        const auto common_header_uv{proxy.get_common_header_updatable_view()};
        const auto total_size{std::size(proxy.get_updatable_portion())};
        if (normalized_mode != materialization_type::leave_checksum_as_is) {
          common_header_uv.set_event_size_raw(
              static_cast<std::uint32_t>(total_size));
        }
        common_header_uv.set_next_event_position_raw(
            static_cast<std::uint32_t>(offset + total_size));
      });
}

[[nodiscard]] event_updatable_view
rewriter::prepare_generic_materialize_internal(const event_view &event_v,
                                               event_storage &buffer,
                                               materialization_type &mode) {
  // mode adjustments for cases when nothing has to be changed
  if (mode == materialization_type::force_remove_checksum &&
      !event_v.has_footer()) {
    mode = materialization_type::leave_checksum_as_is;
  }
  if (mode == materialization_type::force_add_checksum &&
      event_v.has_footer()) {
    mode = materialization_type::leave_checksum_as_is;
  }

  std::size_t destination_footer_size{};
  // source does not have checksum, destination should
  switch (mode) {
  case materialization_type::force_add_checksum: {
    // source does not have checksum, destination should
    assert(!event_v.has_footer());
    const auto event_size_with_footer{event_v.get_total_size() +
                                      footer_view_base::size_in_bytes};
    const auto source_portion{event_v.get_portion()};
    buffer.reserve(event_size_with_footer);
    buffer.assign(std::cbegin(source_portion), std::cend(source_portion));
    buffer.resize(event_size_with_footer);
    destination_footer_size = footer_view_base::size_in_bytes;
  } break;
  case materialization_type::force_remove_checksum: {
    // source has checksum, destination should not
    assert(event_v.has_footer());
    const auto event_size_wo_footer{event_v.get_total_size() -
                                    footer_view_base::size_in_bytes};
    const auto source_portion{
        event_v.get_portion().subspan(0U, event_size_wo_footer)};
    buffer.assign(std::cbegin(source_portion), std::cend(source_portion));
    destination_footer_size = 0U;
  } break;
  case materialization_type::leave_checksum_as_is: {
    // either source has checksum and destination should or
    // source does not have checksum and destination should not
    const auto source_portion{event_v.get_portion()};
    buffer.assign(std::cbegin(source_portion), std::cend(source_portion));
    destination_footer_size = event_v.get_footer_size();
  } break;
  }
  const util::byte_span destination_portion{std::begin(buffer),
                                            std::size(buffer)};
  event_updatable_view result{destination_portion,
                              event_v.get_post_header_size(),
                              destination_footer_size};
  return result;
}

[[nodiscard]] event_updatable_view
rewriter::rewrite(seq_no_t last_local_sequence_number,
                  const event_view &current_event_v,
                  binsrv::events::event_storage &buffer, std::uint64_t offset) {
  switch (current_event_v.get_common_header_view().get_type_code()) {
  case code_type::gtid_log:
  case code_type::anonymous_gtid_log:
    return rewrite_gtid_log_internal(last_local_sequence_number,
                                     current_event_v, buffer, offset);
  case code_type::gtid_tagged_log:
    return rewrite_gtid_tagged_log_internal(last_local_sequence_number,
                                            current_event_v, buffer, offset);
    break;
  default:
    return materialize_and_relocate(current_event_v, buffer,
                                    materialization_type::force_add_checksum,
                                    offset);
  }
}

void rewriter::fix_sequence_number_and_last_committed(
    seq_no_t last_local_sequence_number, seq_no_t &remote_sequence_number,
    seq_no_t &remote_last_committed) {
  if (remote_last_committed >= remote_sequence_number) {
    util::exception_location().raise<std::logic_error>(
        "remote last_committed is greater than or equal to remote "
        "sequence_number");
  }
  // calculating delta between the sequence_number and last_committed in
  // remote event post header
  auto delta{remote_sequence_number - remote_last_committed};
  // sequence_number in the rewritten event must be set to the next number
  // after last_local_sequence_number
  remote_sequence_number = last_local_sequence_number + 1ULL;
  // last_committed in the rewritten event must be set to the value which is
  // 'delta' behind the new sequence_number, but not less than 0
  remote_last_committed = remote_sequence_number;
  if (remote_last_committed > delta) {
    remote_last_committed -= delta;
  } else {
    // only the very first event in the binlog file (which has sequence_number
    // equal to 1) can have last_committed equal to 0, for all the other events
    // last_committed must be at least 1
    remote_last_committed = (remote_sequence_number == 1ULL ? 0ULL : 1ULL);
  }
}

[[nodiscard]] event_updatable_view rewriter::rewrite_gtid_log_internal(
    seq_no_t last_local_sequence_number, const event_view &current_event_v,
    binsrv::events::event_storage &buffer, std::uint64_t offset) {
  assert(current_event_v.get_common_header_view().get_type_code() ==
             code_type::gtid_log ||
         current_event_v.get_common_header_view().get_type_code() ==
             code_type::anonymous_gtid_log);
  const auto gtid_log_fixer{
      [offset, last_local_sequence_number](
          materialization_type normalized_mode,
          const event_updatable_view::write_proxy &proxy) {
        const auto common_header_uv{proxy.get_common_header_updatable_view()};
        const auto total_size{std::size(proxy.get_updatable_portion())};

        if (normalized_mode != materialization_type::leave_checksum_as_is) {
          common_header_uv.set_event_size_raw(
              static_cast<std::uint32_t>(total_size));
        }
        common_header_uv.set_next_event_position_raw(
            static_cast<std::uint32_t>(offset + total_size));

        auto post_header_portion{proxy.get_post_header_updatable_raw()};
        // TODO: rework this with direct inplace updates when we implement
        //       post header updatable views (at least for GTID_LOG and
        //       ANONYMOUS_GTID_LOG events)

        // instantiating an instance of gtid_log_post_header (works for both
        // GTID_LOG and ANONYMOUS_GTID_LOG) from the event post_header data span
        gtid_log_post_header updated_gtid_log_post_header{post_header_portion};

        // extracting and fixing the sequence_number and last_committed values
        // based on the value of the last written sequence_number in the binlog
        // (last_local_sequence_number)
        auto remote_sequence_number{
            updated_gtid_log_post_header.get_sequence_number()};
        auto remote_last_committed{
            updated_gtid_log_post_header.get_last_committed()};
        fix_sequence_number_and_last_committed(last_local_sequence_number,
                                               remote_sequence_number,
                                               remote_last_committed);
        updated_gtid_log_post_header.set_sequence_number(
            remote_sequence_number);
        updated_gtid_log_post_header.set_last_committed(remote_last_committed);
        // serializing the updated post header back to the event post_header
        // data span
        updated_gtid_log_post_header.encode_to(post_header_portion);
      }};
  return generic_materialize(current_event_v, buffer,
                             materialization_type::force_add_checksum,
                             gtid_log_fixer);
}

[[nodiscard]] event_updatable_view rewriter::rewrite_gtid_tagged_log_internal(
    seq_no_t last_local_sequence_number, const event_view &current_event_v,
    binsrv::events::event_storage &buffer, std::uint64_t offset) {
  assert(current_event_v.get_common_header_view().get_type_code() ==
         code_type::gtid_tagged_log);

  // the original size of the current event
  const std::size_t original_event_size{current_event_v.get_total_size()};

  generic_body_impl<code_type::gtid_tagged_log> updated_gtid_tagged_log_body{
      current_event_v.get_body_raw()};
  // the original size of the transaction extracted from the event body
  const std::uint64_t original_transaction_length{
      updated_gtid_tagged_log_body.get_transaction_length_raw()};
  if (original_transaction_length < original_event_size) {
    util::exception_location().raise<std::logic_error>(
        "transaction length in the gtid tagged log event is less than the "
        "event size");
  }

  // the accumulated size of all the events in the transaction apart from the
  // current one
  const std::uint64_t transaction_tail_length{original_transaction_length -
                                              original_event_size};

  // extracting and fixing the sequence_number and last_committed values
  // based on the value of the last written sequence_number in the binlog
  // (last_local_sequence_number)
  auto remote_sequence_number{
      updated_gtid_tagged_log_body.get_sequence_number()};
  auto remote_last_committed{updated_gtid_tagged_log_body.get_last_committed()};
  fix_sequence_number_and_last_committed(last_local_sequence_number,
                                         remote_sequence_number,
                                         remote_last_committed);
  updated_gtid_tagged_log_body.set_sequence_number(remote_sequence_number);
  updated_gtid_tagged_log_body.set_last_committed(remote_last_committed);

  // Because of the updated fields, which are serialized using variable-length
  // encoding, the size of the event may change. We need to recalculate the
  // event size taking into account that a change in the event size should also
  // be reflected in the transaction_length field in the event body, which in
  // turn may also change its size and so on. To handle this, we are
  // recalculating the event size in a loop until it stabilizes (realistically,
  // it should converge in 2 iterations max).
  const auto event_size_calculator{
      [&updated_gtid_tagged_log_body,
       transaction_tail_length](std::size_t event_size_candidate) {
        // post_header size of the GTID_TAGGED_LOG event is zero (so not
        // included in calculations)
        updated_gtid_tagged_log_body.set_transaction_length_raw(
            event_size_candidate + transaction_tail_length);
        return default_common_header_length +
               updated_gtid_tagged_log_body.calculate_encoded_size() +
               default_footer_length;
      }};
  std::size_t event_size_candidate{original_event_size};
  std::size_t recalculated_event_size{};
  std::size_t iteration{0U};
  static constexpr std::size_t max_number_of_iterations{9U};
  while (((recalculated_event_size = event_size_calculator(
               event_size_candidate)) != event_size_candidate) &&
         (iteration < max_number_of_iterations)) {
    event_size_candidate = recalculated_event_size;
    ++iteration;
  }
  assert(iteration < max_number_of_iterations);
  // After this loop both recalculated_event_size and event_size_candidate
  // variables hold the final stabilized size of the rewritten event, which is
  // consistent with the transaction_length field in the event body. Moreover,
  // the transaction_length field in the event body has also been updated
  // accordingly.

  // assembling the rewritten event in the provided buffer
  // recalculated_event_size includes
  // common_header size(19) + post_header size (0) + body size (variable) +
  // footer size (4)
  buffer.resize(recalculated_event_size);
  // copying common_header from the original event to the buffer
  std::ranges::copy(current_event_v.get_common_header_raw(), buffer.begin());
  // no need to copy post_header as its size is zero for GTID_TAGGED_LOG events
  event_updatable_view result{buffer, 0U, default_footer_length};
  {
    // creating a write proxy here so that the crc in the footer is recalculated
    // at the end of this block when the proxy is destroyed
    const auto write_proxy{result.get_write_proxy()};
    const auto common_header_uv{write_proxy.get_common_header_updatable_view()};
    // updating the event_size and next_event_position fields in the common
    // header based on the recalculated event size
    common_header_uv.set_event_size_raw(
        static_cast<std::uint32_t>(recalculated_event_size));
    common_header_uv.set_next_event_position_raw(
        static_cast<std::uint32_t>(offset + recalculated_event_size));

    // encoding the updated body (with fixed sequence_number and last_committed
    // values, and possibly updated transaction_length value) to the body part
    // of the buffer
    auto body_portion{write_proxy.get_body_updatable_raw()};
    updated_gtid_tagged_log_body.encode_to(body_portion);
  }
  return result;
}

} // namespace binsrv::events
