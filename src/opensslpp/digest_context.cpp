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

#include "opensslpp/digest_context.hpp"

#include <cassert>
#include <cstddef>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

#include <openssl/evp.h>
#include <openssl/types.h>

#include "opensslpp/core_error.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace opensslpp {

struct digest_context::native_helper {
  [[nodiscard]] static auto deimpl(auto &impl) noexcept {
    using cast_type = std::conditional_t<
        std::is_const_v<std::remove_reference_t<decltype(impl)>>,
        const EVP_MD_CTX, EVP_MD_CTX>;
    return static_cast<cast_type *>(impl.get());
  }

  [[nodiscard]] static const EVP_MD *
  get_md_by_code_internal(digest_code_type code) noexcept {
    switch (code) {
    case digest_code_type::sha256:
      return EVP_sha256();
    default:
      return nullptr;
    }
  }

  [[nodiscard]] static const EVP_MD *
  get_validated_md_by_code_internal(digest_code_type code) {
    const auto *digest{get_md_by_code_internal(code)};
    if (digest == nullptr) {
      util::exception_location().raise<core_error>("unknown digest code");
    }
    return digest;
  }
};

void digest_context::impl_deleter::operator()(void *digest_ctx) const noexcept {
  if (digest_ctx != nullptr) {
    EVP_MD_CTX_free(static_cast<EVP_MD_CTX *>(digest_ctx));
  }
}

digest_context::digest_context(digest_code_type code) : code_{code} {
  const auto *digest{native_helper::get_validated_md_by_code_internal(code)};

  impl_ptr new_impl{EVP_MD_CTX_new(), impl_deleter{}};
  if (!new_impl) {
    util::exception_location().raise<core_error>(
        "cannot allocate digest context");
  }

  if (EVP_DigestInit_ex(static_cast<EVP_MD_CTX *>(new_impl.get()), digest,
                        nullptr) == 0) {
    util::exception_location().raise<core_error>(
        "cannot initialize digest context");
  }

  impl_ = std::move(new_impl);
}

std::size_t digest_context::get_digest_size_in_bytes() const noexcept {
  assert(impl_);
  const auto native_result{EVP_MD_CTX_size(native_helper::deimpl(impl_))};
  assert(native_result != -1);
  return static_cast<std::size_t>(native_result);
}

std::size_t
digest_context::get_digest_size_in_bytes(digest_code_type code) noexcept {
  const auto *digest{native_helper::get_md_by_code_internal(code)};
  if (digest == nullptr) {
    return 0U;
  }
  return static_cast<std::size_t>(EVP_MD_get_size(digest));
}

void digest_context::update(util::const_byte_span input) {
  assert(impl_);
  if (std::empty(input)) {
    return;
  }
  if (EVP_DigestUpdate(native_helper::deimpl(impl_), std::data(input),
                       std::size(input)) == 0) {
    util::exception_location().raise<core_error>(
        "cannot update digest context");
  }
}

void digest_context::finalize(util::byte_span output) {
  assert(impl_);
  const auto expected_size{get_digest_size_in_bytes()};
  if (std::size(output) != expected_size) {
    util::exception_location().raise<core_error>(
        "digest output buffer has unexpected size");
  }

  unsigned int native_size{0U};
  if (EVP_DigestFinal_ex(
          native_helper::deimpl(impl_),
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<unsigned char *>(std::data(output)),
          &native_size) == 0) {
    util::exception_location().raise<core_error>(
        "cannot finalize digest context");
  }
  assert(static_cast<std::size_t>(native_size) == expected_size);

  // discard the finalized context: any further update()/finalize() call must
  // start with a fresh EVP_MD_CTX (users can re-initialize via move-assign
  // from a newly constructed digest_context).
  impl_.reset();
}

std::string digest_context::calculate(digest_code_type code,
                                      util::const_byte_span input) {
  digest_context ctx{code};
  ctx.update(input);
  std::string result(ctx.get_digest_size_in_bytes(), '\0');
  ctx.finalize(util::byte_span{
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<std::byte *>(std::data(result)), std::size(result)});
  return result;
}

} // namespace opensslpp
