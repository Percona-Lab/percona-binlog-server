#include "binsrv/event/protocol_traits.hpp"

#include <cstddef>

#include "binsrv/event/code_type.hpp"

#include "util/conversion_helpers.hpp"

namespace binsrv::event {

[[nodiscard]] std::size_t
get_post_header_length_for_code(const post_header_length_container &storage,
                                code_type code) noexcept {
  static_assert(default_number_of_events ==
                    util::enum_to_index(code_type::delimiter),
                "mismatch between number_of_events and code_type enum");

  // here the very first "unknown" code is not included in the array by the
  // spec
  const auto index{util::enum_to_index(code)};
  if (index == 0U || index >= default_number_of_events) {
    return 0U;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  return static_cast<std::size_t>(storage[index - 1U]);
}

} // namespace binsrv::event
