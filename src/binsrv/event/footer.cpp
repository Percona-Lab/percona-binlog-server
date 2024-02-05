#include "binsrv/event/footer.hpp"

#include <iomanip>
#include <ios>
#include <iterator>
#include <ostream>
#include <stdexcept>

#include <boost/io_fwd.hpp>

#include <boost/align/align_up.hpp>

#include <boost/io/ios_state.hpp>

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

std::ostream &operator<<(std::ostream &output, const footer &obj) {
  const boost::io::ios_flags_saver flag_saver(output);
  const boost::io::ios_fill_saver fill_saver(output);
  return output << "crc: " << std::hex << std::showbase << std::setfill('0')
                << std::setw(sizeof(obj.get_crc_raw()) * 2)
                << obj.get_crc_raw();
}

} // namespace binsrv::event
