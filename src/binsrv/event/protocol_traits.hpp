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
