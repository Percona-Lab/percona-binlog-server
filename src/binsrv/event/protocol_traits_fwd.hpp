#ifndef BINSRV_EVENT_PROTOCOL_TRAITS_FWD_HPP
#define BINSRV_EVENT_PROTOCOL_TRAITS_FWD_HPP

#include <cstddef>
#include <cstdint>

namespace binsrv::event {

inline constexpr std::uint16_t default_binlog_version{4U};
inline constexpr std::size_t default_number_of_events{42U};
inline constexpr std::size_t default_common_header_length{19U};

} // namespace binsrv::event

#endif // BINSRV_EVENT_PROTOCOL_TRAITS_FWD_HPP
