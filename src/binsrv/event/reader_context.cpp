#include "binsrv/event/reader_context.hpp"

#include "binsrv/event/checksum_algorithm_type.hpp"
#include "binsrv/event/code_type.hpp"
#include "binsrv/event/event.hpp"

namespace binsrv::event {

reader_context::reader_context()
    : checksum_algorithm_{checksum_algorithm_type::off} {}

void reader_context::process_event(const event &current_event) {
  // TODO: add some kind of state machine where the expected sequence of events
  //       is the following -
  //       (ROTATE(artificial) FORMAT_DESCRIPTION <ANY>* ROTATE)*
  if (current_event.get_common_header().get_type_code() ==
      code_type::format_description) {
    const auto &post_header{
        current_event.get_post_header<code_type::format_description>()};
    const auto &body{current_event.get_body<code_type::format_description>()};

    // TODO: check if binlog_version == default_binlog_version
    // TODO: check if common_header_length == default_common_header_length
    // TODO: check if post_header_lengths for known events has expected
    //       generic_post_header_impl<code_type::xxx>::size_in_bytes
    //       (at least for 'format_description' and 'rotate')

    fde_processed_ = true;
    post_header_lengths_ = post_header.get_post_header_lengths_raw();
    checksum_algorithm_ = body.get_checksum_algorithm();
  }
  // TODO: add position counter and check if common_header.event_size /
  //       common_header. next_event_position match this counter
  // TODO: check if CRC32 checksum from the footer (if present) matches the
  //       calculated one
}

} // namespace binsrv::event
