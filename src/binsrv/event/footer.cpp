#include "binsrv/event/footer.hpp"

#include <iterator>
#include <stdexcept>

#include <boost/align/align_up.hpp>

#include "util/byte_span_extractors.hpp"
#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv::event {

footer::footer(util::const_byte_span portion) {
  // TODO: rework with direct member initialization

  // TODO: initialize size_in_bytes directly based on the sum of fields
  // widths instead of this static_assert
  static_assert(sizeof crc_ == size_in_bytes,
                "mismatch in footer::size_in_bytes");
  // make sure we did OK with data members reordering
  static_assert(sizeof *this == boost::alignment::align_up(
                                    size_in_bytes, alignof(decltype(*this))),
                "inefficient data member reordering in footer");

  if (std::size(portion) != size_in_bytes) {
    util::exception_location().raise<std::invalid_argument>(
        "invalid event footer length");
  }

  auto remainder = portion;
  util::extract_fixed_int_from_byte_span(remainder, crc_);
}

} // namespace binsrv::event
