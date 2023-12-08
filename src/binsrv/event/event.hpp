#ifndef BINSRV_EVENT_EVENT_HPP
#define BINSRV_EVENT_EVENT_HPP

#include "binsrv/event/event_fwd.hpp" // IWYU pragma: export

#include <array>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <boost/mp11/algorithm.hpp>
#include <boost/mp11/integer_sequence.hpp>
#include <boost/mp11/list.hpp>

#include "binsrv/event/code_type.hpp"
#include "binsrv/event/common_header.hpp"                // IWYU pragma: export
#include "binsrv/event/footer.hpp"                       // IWYU pragma: export
#include "binsrv/event/format_description_body_impl.hpp" // IWYU pragma: export
#include "binsrv/event/format_description_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/event/generic_body.hpp"            // IWYU pragma: export
#include "binsrv/event/generic_post_header.hpp"     // IWYU pragma: export
#include "binsrv/event/rotate_body_impl.hpp"        // IWYU pragma: export
#include "binsrv/event/rotate_post_header_impl.hpp" // IWYU pragma: export
#include "binsrv/event/unknown_body.hpp"            // IWYU pragma: export
#include "binsrv/event/unknown_post_header.hpp"     // IWYU pragma: export

#include "util/byte_span_fwd.hpp"
#include "util/conversion_helpers.hpp"

namespace binsrv::event {

using optional_format_description_post_header =
    std::optional<generic_post_header<code_type::format_description>>;
using optional_format_description_body =
    std::optional<generic_body<code_type::format_description>>;

class [[nodiscard]] event {
private:
  static constexpr std::size_t number_of_events{
      util::enum_to_index(code_type::delimiter)};

  // here we create an index sequence (std::index_sequence) specialized
  // with the following std::size_t constant pack: 0 .. <number_of_events>
  using code_index_sequence = std::make_index_sequence<number_of_events>;
  // almost all boost::mp11 algorithms accept lists of types (rather than
  // lists of constants), so we convert std::index_sequence into
  // boost::mp11::mp_list of std::integral_constant types
  using wrapped_code_index_sequence =
      boost::mp11::mp_from_sequence<code_index_sequence>;

  // the following alias template is used later as a boost::mp11
  // metafunction: it transforms an Index (represented via
  // std::integral_constant) into one of the post header specializations
  template <typename Index>
  using index_to_post_header_mf =
      generic_post_header<util::index_to_enum<code_type>(Index::value)>;
  // using the list of integral constants and the
  // "event code" -> "post header specialization" conversion metafunction,
  // we create a type list of all possible post header specializations
  // (containing duplicates)
  using post_header_type_list =
      boost::mp11::mp_transform<index_to_post_header_mf,
                                wrapped_code_index_sequence>;
  // here we remove all the duplicates from the type list above
  using unique_post_header_type_list =
      boost::mp11::mp_unique<post_header_type_list>;
  // and finally we rename the type list (with unique types) into
  // std::variant
  using post_header_variant =
      boost::mp11::mp_rename<unique_post_header_type_list, std::variant>;

  // identical techniqure is used to obtain the std::variant of all possible
  // unique event bodyies
  template <typename Index>
  using index_to_body_mf =
      generic_body<util::index_to_enum<code_type>(Index::value)>;
  using body_type_list =
      boost::mp11::mp_transform<index_to_body_mf, wrapped_code_index_sequence>;
  using unique_body_type_list = boost::mp11::mp_unique<body_type_list>;
  using body_variant =
      boost::mp11::mp_rename<unique_body_type_list, std::variant>;

  using optional_footer = std::optional<footer>;

public:
  event(util::const_byte_span portion,
        const optional_format_description_post_header &fde_post_header,
        const optional_format_description_body &fde_body);

  [[nodiscard]] const common_header &get_common_header() const noexcept {
    return common_header_;
  }
  [[nodiscard]] const post_header_variant &get_post_header() const noexcept {
    return post_header_;
  }
  [[nodiscard]] const body_variant &get_body() const noexcept { return body_; }
  [[nodiscard]] const optional_footer &get_footer() const noexcept {
    return footer_;
  }

private:
  common_header common_header_;
  post_header_variant post_header_;
  body_variant body_;
  optional_footer footer_;

  template <typename T>
  void generic_emplace_post_header(util::const_byte_span portion) {
    post_header_.emplace<T>(portion);
  }
  void emplace_post_header(code_type code, util::const_byte_span portion);
  template <typename T>
  void generic_emplace_body(util::const_byte_span portion) {
    body_.emplace<T>(portion);
  }
  void emplace_body(code_type code, util::const_byte_span portion);
};

} // namespace binsrv::event

#endif // BINSRV_EVENT_EVENT_HPP
