#ifndef BINSRV_EVENT_PROTOCOL_TRAITS_HPP
#define BINSRV_EVENT_PROTOCOL_TRAITS_HPP

#include "binsrv/event/protocol_traits_fwd.hpp" // IWYU pragma: export

#include <array>
#include <cstddef>
#include <cstdint>

#include "binsrv/event/code_type_fwd.hpp"

namespace binsrv::event {

// we do not store length for the first element which is the "unknown" event
using post_header_length_container =
    std::array<std::uint8_t, default_number_of_events - 1U>;

[[nodiscard]] std::size_t
get_post_header_length_for_code(const post_header_length_container &storage,
                                code_type code) noexcept;

} // namespace binsrv::event

#endif // BINSRV_EVENT_PROTOCOL_TRAITS_HPP
