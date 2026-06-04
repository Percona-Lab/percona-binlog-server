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

#ifndef BINSRV_EVENTS_REWRITER_HPP
#define BINSRV_EVENTS_REWRITER_HPP

#include "binsrv/events/rewriter_fwd.hpp" // IWYU pragma: export

#include <cstdint>

#include "binsrv/events/common_types.hpp"
#include "binsrv/events/event_fwd.hpp"
#include "binsrv/events/event_view.hpp"

namespace binsrv::events {

class rewriter {
public:
  // Generic materialization function that will create a copy of the event data
  // from the 'event_v' and put it into the 'buffer' with the following
  // adjustments:
  // - if 'mode' is 'force_add_checksum', the materialized event will have a
  //   footer with a checksum even if the original event does not have it
  // - if 'mode' is 'force_remove_checksum', the materialized event will not
  //   have a footer with a checksum even if the original event has it
  // - if 'mode' is 'leave_checksum_as_is', the materialized event will have a
  //   footer with a checksum if and only if the original event has it.
  // This function will also call a generic 'modification_functor' that allows
  // to perform any additional modifications to the materialized event (e.g.
  // change some fields in the common header, post header or body. The signature
  // of the 'modification_functor' should be the following:
  // void modify(
  //   materialization_type,
  //   const event_updatable_view::write_proxy
  // );
  template <typename ModificationFunctor>
  [[nodiscard]] static event_updatable_view
  generic_materialize(const event_view &event_v, event_storage &buffer,
                      materialization_type mode,
                      const ModificationFunctor &modification_functor);

  // Simplified materialization functions for one of the most common use cases
  // when no additional modifications are needed (only 'event_size' in the
  // common header may be updated if the footer was added or removed).
  [[nodiscard]] static event_updatable_view
  materialize(const event_view &event_v, event_storage &buffer,
              materialization_type mode);

  // Simplified materialization functions for one of the most common use cases
  // when the event needs to be put into a datafile at the specified 'offset'
  // (in this case 'next_event_position' in the common header will be updated
  // and 'event_size' in the common header may be updated if the footer was
  // added or removed).
  [[nodiscard]] static event_updatable_view
  materialize_and_relocate(const event_view &event_v, event_storage &buffer,
                           materialization_type mode, std::uint64_t offset);

  // This methods performs required adjustments for event relocation:
  // - for all events it updates next_event_position field in the
  //   common header
  // - for all events it adds a footer with the checksum and
  //   changes event_size in the common header if it is missing
  // - for GTID_LOG and ANONYMOUS_GTID_LOG events it updates sequence_number
  //   and last_committed fields in the event post header
  // - for GTID_TAGGED_LOG events it updates sequence_number and
  //   last_committed fields in the event body and may change
  //   transaction_length field in the event body and event size in the
  //   common header accordingly (because of variable length encoding of
  // the event body elements)
  [[nodiscard]] static event_updatable_view
  rewrite(seq_no_t last_local_sequence_number,
          const event_view &current_event_v,
          binsrv::events::event_storage &buffer, std::uint64_t offset);

private:
  [[nodiscard]] static event_updatable_view
  prepare_generic_materialize_internal(const event_view &event_v,
                                       event_storage &buffer,
                                       materialization_type &mode);
  // fixes the sequence_number and last_committed values extracted from
  // GTID_LOG and ANONYMOUS_GTID_LOG events based on the value of the last
  // written sequence_number in the binlog (last_local_sequence_number)
  static void
  fix_sequence_number_and_last_committed(seq_no_t last_local_sequence_number,
                                         seq_no_t &remote_sequence_number,
                                         seq_no_t &remote_last_committed);

  [[nodiscard]] static event_updatable_view rewrite_gtid_log_internal(
      seq_no_t last_local_sequence_number, const event_view &current_event_v,
      binsrv::events::event_storage &buffer, std::uint64_t offset);

  [[nodiscard]] static event_updatable_view rewrite_gtid_tagged_log_internal(
      seq_no_t last_local_sequence_number, const event_view &current_event_v,
      binsrv::events::event_storage &buffer, std::uint64_t offset);
};

template <typename ModificationFunctor>
[[nodiscard]] event_updatable_view
rewriter::generic_materialize(const event_view &event_v, event_storage &buffer,
                              materialization_type mode,
                              const ModificationFunctor &modification_functor) {
  // mode may be modified (normalized) by this call
  const auto result{
      prepare_generic_materialize_internal(event_v, buffer, mode)};
  {
    // we are creating a write_proxy here so that the checksum will be properly
    // recalculated after all modifications
    const auto write_proxy{result.get_write_proxy()};
    modification_functor(mode, write_proxy);
  }
  return result;
}

} // namespace binsrv::events

#endif // BINSRV_EVENTS_REWRITER_HPP
