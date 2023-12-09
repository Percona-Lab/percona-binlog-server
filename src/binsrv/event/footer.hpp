#ifndef BINSRV_EVENT_FOOTER_HPP
#define BINSRV_EVENT_FOOTER_HPP

#include "binsrv/event/footer_fwd.hpp" // IWYU pragma: export

#include <cstdint>

#include "util/byte_span_fwd.hpp"

namespace binsrv::event {

class [[nodiscard]] footer {
public:
  static constexpr std::size_t size_in_bytes{4U};

  explicit footer(util::const_byte_span portion);

  [[nodiscard]] std::uint32_t get_crc_raw() const noexcept { return crc_; }

private:
  std::uint32_t crc_{};
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_FOOTER_HPP
