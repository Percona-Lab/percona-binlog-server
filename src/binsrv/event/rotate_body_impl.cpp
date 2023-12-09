#include "binsrv/event/rotate_body_impl.hpp"

#include "binsrv/event/code_type.hpp"

#include "util/byte_span.hpp"

namespace binsrv::event {

generic_body_impl<code_type::rotate>::generic_body_impl(
    util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  // no need to check if member reordering is OK as this class has
  // only one member for holding data of varying length

  binlog_.assign(util::as_string_view(portion));
}

} // namespace binsrv::event
