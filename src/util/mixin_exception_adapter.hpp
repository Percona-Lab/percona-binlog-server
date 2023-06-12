#ifndef UTIL_MIXIN_EXCEPTION_ADAPTER_HPP
#define UTIL_MIXIN_EXCEPTION_ADAPTER_HPP

#include <concepts>
#include <exception>
#include <utility>

namespace util {

template <std::derived_from<std::exception> Exception, typename Mixin>
class [[nodiscard]] mixin_exception_adapter : public Exception, public Mixin {
public:
  using base_type = Exception;
  using mixin_type = Mixin;

  template <typename MixinArg, typename... TT>
  explicit mixin_exception_adapter(MixinArg &&mixin, TT &&...args)
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
      : base_type{std::forward<TT>(args)...},
        mixin_type{std::forward<MixinArg>(mixin)} {}

  [[nodiscard]] mixin_type &get_mixin() noexcept {
    return *static_cast<mixin_type *>(this);
  }
  [[nodiscard]] const mixin_type &get_mixin() const noexcept {
    return *static_cast<const mixin_type *>(this);
  }
};

} // namespace util

#endif // UTIL_MIXIN_EXCEPTION_ADAPTER_HPP
