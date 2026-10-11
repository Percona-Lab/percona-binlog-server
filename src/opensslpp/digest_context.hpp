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

#ifndef OPENSSLPP_DIGEST_CONTEXT_HPP
#define OPENSSLPP_DIGEST_CONTEXT_HPP

#include "opensslpp/digest_context_fwd.hpp" // IWYU pragma: export

#include <cstddef>
#include <memory>
#include <string>

#include "util/byte_span_fwd.hpp"

namespace opensslpp {

class digest_context {
public:
  digest_context() noexcept = default;
  // 'digest_name' must be a valid digest name supported by OpenSSL
  // (e.g. "SHA256")
  explicit digest_context(const std::string &digest_name);
  ~digest_context() noexcept = default;

  digest_context(const digest_context &) = delete;
  digest_context(digest_context &&) noexcept = default;
  digest_context &operator=(const digest_context &) = delete;
  digest_context &operator=(digest_context &&) noexcept = default;

  void swap(digest_context &obj) noexcept { impl_.swap(obj.impl_); }

  [[nodiscard]] bool is_empty() const noexcept { return !impl_; }

  [[nodiscard]] std::size_t get_digest_size_in_bytes() const noexcept;

  [[nodiscard]] static std::size_t
  get_digest_size_in_bytes(const std::string &digest_name) noexcept;

  void update(util::const_byte_span input);
  // 'output' must be exactly get_digest_size_in_bytes() bytes long.
  void finalize(util::byte_span output);

  [[nodiscard]] static digest_storage calculate(const std::string &digest_name,
                                                util::const_byte_span input);

private:
  struct native_helper;
  struct impl_deleter {
    void operator()(void *digest_ctx) const noexcept;
  };
  using impl_ptr = std::unique_ptr<void, impl_deleter>;
  impl_ptr impl_;
};

} // namespace opensslpp

#endif // OPENSSLPP_DIGEST_CONTEXT_HPP
