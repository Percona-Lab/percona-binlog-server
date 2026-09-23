// Copyright (c) 2023-2026 Percona and/or its affiliates.
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
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <openssl/evp.h>
#include <openssl/types.h>

#include "opensslpp/core_error.hpp"

#include "util/exception_location_helpers.hpp"

namespace opensslpp {

void digest_context::impl_deleter::operator()(void *digest_ctx) const noexcept {
  if (digest_ctx != nullptr) {
    EVP_MD_CTX_free(static_cast<EVP_MD_CTX *>(digest_ctx));
  }
}

digest_context::digest_context(const std::string &digest_name) {
  const auto *digest{EVP_get_digestbyname(digest_name.c_str())};
  if (digest == nullptr) {
    util::exception_location().raise<core_error>("unknown digest name");
  }

  std::unique_ptr<void, impl_deleter> new_impl{EVP_MD_CTX_new(),
                                               impl_deleter{}};
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

void digest_context::update(std::string_view data) {
  assert(impl_);
  if (std::empty(data)) {
    return;
  }
  if (EVP_DigestUpdate(static_cast<EVP_MD_CTX *>(impl_.get()), std::data(data),
                       std::size(data)) == 0) {
    util::exception_location().raise<core_error>(
        "cannot update digest context");
  }
}

std::string digest_context::finalize() {
  assert(impl_);
  auto *ctx{static_cast<EVP_MD_CTX *>(impl_.get())};
  std::string result(static_cast<std::size_t>(EVP_MD_CTX_size(ctx)), '\0');
  unsigned int size{0U};
  if (EVP_DigestFinal_ex(
          ctx,
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<unsigned char *>(std::data(result)), &size) == 0) {
    util::exception_location().raise<core_error>(
        "cannot finalize digest context");
  }
  assert(static_cast<std::size_t>(size) == std::size(result));
  impl_.reset();
  return result;
}

std::string digest_context::calculate(const std::string &digest_name,
                                      std::string_view data) {
  digest_context ctx{digest_name};
  ctx.update(data);
  return ctx.finalize();
}

} // namespace opensslpp
