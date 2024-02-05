#ifndef BINSRV_EVENT_PROTOCOL_TRAITS_HPP
#define BINSRV_EVENT_PROTOCOL_TRAITS_HPP

#include "binsrv/event/protocol_traits_fwd.hpp" // IWYU pragma: export

#include <array>
#include <cstddef>
#include <cstdint>

#include "binsrv/event/code_type_fwd.hpp"

namespace binsrv::event {

using encoded_post_header_length_type = std::uint8_t;

// we do not store length for the first element which is the "unknown" event
using post_header_length_container =
    std::array<encoded_post_header_length_type, default_number_of_events - 1U>;

[[nodiscard]] std::size_t
get_post_header_length_for_code(const post_header_length_container &storage,
                                code_type code) noexcept;

std::ostream &
print_post_header_lengths(std::ostream &output,
                          const post_header_length_container &obj);

void validate_post_header_lengths(
    const post_header_length_container &runtime,
    const post_header_length_container &hardcoded);

} // namespace binsrv::event

#endif // BINSRV_EVENT_PROTOCOL_TRAITS_HPP
