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

#include <memory>
#include <string>
#include <string_view>

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

  [[nodiscard]] bool is_empty() const noexcept { return !impl_; }

  void update(std::string_view data);
  [[nodiscard]] std::string finalize();

  [[nodiscard]] static std::string calculate(const std::string &digest_name,
                                             std::string_view data);

private:
  struct impl_deleter {
    void operator()(void *digest_ctx) const noexcept;
  };
  std::unique_ptr<void, impl_deleter> impl_;
};

} // namespace opensslpp

#endif // OPENSSLPP_DIGEST_CONTEXT_HPP
