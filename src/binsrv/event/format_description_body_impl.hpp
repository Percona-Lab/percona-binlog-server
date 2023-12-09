#ifndef BINSRV_EVENT_FORMAT_DESCRIPTION_BODY_IMPL_HPP
#define BINSRV_EVENT_FORMAT_DESCRIPTION_BODY_IMPL_HPP

#include "binsrv/event/format_description_body_impl_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "binsrv/event/checksum_algorithm_type_fwd.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/conversion_helpers.hpp"

namespace binsrv::event {

template <>
class [[nodiscard]] generic_body_impl<code_type::format_description> {
public:
  static constexpr std::size_t size_in_bytes{1U};

  explicit generic_body_impl(util::const_byte_span portion);

  [[nodiscard]] std::uint8_t get_checksum_algorithm_raw() const noexcept {
    return checksum_algorithm_;
  }
  [[nodiscard]] checksum_algorithm_type
  get_checksum_algorithm() const noexcept {
    return util::index_to_enum<checksum_algorithm_type>(
        get_checksum_algorithm_raw());
  }
  [[nodiscard]] std::string_view
  get_readable_checksum_algorithm() const noexcept;

private:
  std::uint8_t checksum_algorithm_{};
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_FORMAT_DESCRIPTION_BODY_IMPL_HPP
