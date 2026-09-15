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

#ifndef OPENSSLPP_DIGEST_CONTEXT_HPP
#define OPENSSLPP_DIGEST_CONTEXT_HPP

#include "opensslpp/digest_context_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <memory>
#include <string>

#include "util/byte_span_fwd.hpp"

namespace opensslpp {

// Thin RAII wrapper around OpenSSL's EVP_MD_CTX for message-digest
// computation. Feed input via update() and read out the fixed-length digest
// via finalize(); or use the one-shot calculate() helper when the whole input
// is already in memory.
class digest_context {
public:
  digest_context() noexcept = default;
  // * 'code' must be one of the supported digest_code_type values (currently
  //   only sha256).
  explicit digest_context(digest_code_type code);
  ~digest_context() noexcept = default;

  digest_context(const digest_context &obj) = delete;
  digest_context(digest_context &&obj) noexcept = default;

  digest_context &operator=(const digest_context &obj) = delete;
  digest_context &operator=(digest_context &&obj) noexcept = default;

  void swap(digest_context &obj) noexcept {
    impl_.swap(obj.impl_);
    std::swap(code_, obj.code_);
  }

  [[nodiscard]] bool is_empty() const noexcept { return !impl_; }

  [[nodiscard]] digest_code_type get_code() const noexcept { return code_; }
  [[nodiscard]] std::size_t get_digest_size_in_bytes() const noexcept;

  // static variant that maps a digest_code_type to its output length without
  // needing a live context (mirrors cipher_context::get_key_size_in_bytes())
  [[nodiscard]] static std::size_t
  get_digest_size_in_bytes(digest_code_type code) noexcept;

  void update(util::const_byte_span input);

  // 'output' must be exactly get_digest_size_in_bytes() bytes long. The
  // context is reset after finalization so the object can be reused (a fresh
  // update()/finalize() sequence starts a new digest with the same code).
  void finalize(util::byte_span output);

  // One-shot helper that sizes and allocates the output string and returns
  // the raw digest bytes.
  [[nodiscard]] static std::string calculate(digest_code_type code,
                                             util::const_byte_span input);

private:
  struct native_helper;
  struct impl_deleter {
    void operator()(void *digest_ctx) const noexcept;
  };

  using impl_ptr = std::unique_ptr<void, impl_deleter>;
  impl_ptr impl_;
  digest_code_type code_{digest_code_type::delimiter};
};

} // namespace opensslpp

#endif // OPENSSLPP_DIGEST_CONTEXT_HPP
