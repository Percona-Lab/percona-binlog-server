#ifndef BINSRV_LOG_SEVERITY_FWD_HPP
#define BINSRV_LOG_SEVERITY_FWD_HPP

#include <concepts>
#include <cstdint>
#include <iosfwd>

namespace binsrv {

enum class log_severity : std::uint8_t;

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_ostream<Char, Traits> &
operator<<(std::basic_ostream<Char, Traits> &output, log_severity level);

template <typename Char, typename Traits>
  requires std::same_as<Char, char>
std::basic_istream<Char, Traits> &
operator>>(std::basic_istream<Char, Traits> &input, log_severity &level);

} // namespace binsrv

#endif // BINSRV_LOG_SEVERITY_FWD_HPP
