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

#include "minimysql/caching_sha2_password_authenticator.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <openssl/evp.h>
#include <openssl/types.h>

namespace {

enum class digest_code_type : std::uint8_t {
  sha256,
};

class digest_context {
public:
  // no std::string_view for 'type' as we need it to be nul-terminated
  explicit digest_context(digest_code_type digest_code)
      : impl_{EVP_MD_CTX_new(), digest_context_deleter{}} {
    if (!impl_) {
      throw std::runtime_error{"failed to create digest context"};
    }
    if (EVP_DigestInit_ex(impl_.get(), get_md_by_digest_code(digest_code),
                          nullptr) == 0) {
      throw std::runtime_error{"failed to initialize digest context"};
    }
  }

  ~digest_context() noexcept = default;

  digest_context(const digest_context &obj) = delete;
  digest_context(digest_context &&obj) noexcept = delete;

  digest_context &operator=(const digest_context &obj) = delete;
  digest_context &operator=(digest_context &&obj) noexcept = delete;

  [[nodiscard]] std::size_t get_size_in_bytes() const noexcept {
    assert(impl_);
    auto native_result{EVP_MD_CTX_size(impl_.get())};
    assert(native_result != -1);
    return static_cast<std::size_t>(native_result);
  }

  void update(std::string_view data) {
    assert(impl_);
    if (EVP_DigestUpdate(impl_.get(), std::data(data), std::size(data)) == 0) {
      throw std::runtime_error{"failed to update digest context"};
    }
  }
  std::string finalize() {
    assert(impl_);
    std::string result(get_size_in_bytes(), '\0');

    unsigned int result_size = 0;
    if (EVP_DigestFinal_ex(
            impl_.get(),
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<unsigned char *>(std::data(result)),
            &result_size) == 0) {
      throw std::runtime_error{"cannot finalize digest context"};
    }
    assert(result_size == std::size(result));

    impl_.reset();
    return result;
  }

private:
  struct digest_context_deleter {
    void operator()(EVP_MD_CTX *digest_context) const noexcept {
      // null-ness is handled by EVP_MD_CTX_free
      EVP_MD_CTX_free(digest_context);
    }
  };

  using impl_ptr = std::unique_ptr<EVP_MD_CTX, digest_context_deleter>;
  impl_ptr impl_;

  [[nodiscard]] static const EVP_MD *
  get_md_by_digest_code(digest_code_type digest_code) noexcept {
    switch (digest_code) {
    case digest_code_type::sha256:
      return EVP_sha256();
    default:
      // should never happen as we only construct digest_context with supported
      // digest_code_type
      return nullptr;
    }
  }
};

std::string calculate_digest(digest_code_type digest_code,
                             std::string_view data) {
  digest_context ctx(digest_code);
  ctx.update(data);
  return ctx.finalize();
}

} // anonymous namespace

namespace minimysql {

std::string caching_sha2_password_authenticator::scramble(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view password, std::string_view salt) {
  // this is how client calculates client_auth_data for caching_sha2_password
  // plugin: SHA256(password) XOR SHA256(SHA256(SHA256(password)),
  // server_auth_data)

  // server, provided that it knows original password and server_auth_data
  // (salt), can verify client_auth_data by calculating the same way and
  // comparing the result with client_auth_data
  const auto digest_code{digest_code_type::sha256};

  // calculating hashed password
  auto result{calculate_digest(digest_code, password)};

  // calculating double-hashed password
  const auto double_hashed_password{calculate_digest(digest_code, result)};

  // calculating salted triple-hashed password
  digest_context ctx(digest_code);
  ctx.update(double_hashed_password);
  ctx.update(salt);
  const auto salted_triple_hashed_password{ctx.finalize()};

  assert(std::size(result) == std::size(salted_triple_hashed_password));
  std::ranges::transform(result, salted_triple_hashed_password,
                         std::begin(result), std::bit_xor<std::uint8_t>{});
  return result;
}

} // namespace minimysql
