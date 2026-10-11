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

#ifndef OPENSSLPP_EVP_PKEY_HPP
#define OPENSSLPP_EVP_PKEY_HPP

#include "opensslpp/evp_pkey_fwd.hpp" // IWYU pragma: export

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "opensslpp/encryption_padding_fwd.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/dynamic_byte_buffer_fwd.hpp"

namespace opensslpp {

class evp_pkey final {
public:
  [[nodiscard]] static const std::string &get_rsa_algorithm_name() {
    static const std::string name{"RSA"};
    return name;
  }

  evp_pkey() noexcept = default;
  ~evp_pkey() noexcept = default;

  evp_pkey(const evp_pkey &obj) = delete;
  evp_pkey(evp_pkey &&obj) noexcept = default;

  evp_pkey &operator=(const evp_pkey &obj) = delete;
  evp_pkey &operator=(evp_pkey &&obj) noexcept = default;

  void swap(evp_pkey &obj) noexcept {
    impl_.swap(obj.impl_);
    std::swap(private_, obj.private_);
  }

  [[nodiscard]] bool is_empty() const noexcept { return !impl_; }
  [[nodiscard]] bool has_private_component() const noexcept {
    assert(!is_empty());
    return private_;
  }

  [[nodiscard]] std::size_t get_size_in_bits() const noexcept;
  [[nodiscard]] std::size_t get_size_in_bytes() const noexcept;

  [[nodiscard]] std::size_t
  get_max_encryption_message_size(encryption_padding padding) const noexcept;

  [[nodiscard]] evp_pkey derive_public_key() const;

  // no std::string_view for 'algorithm_name' as it needs to be null-terminated
  [[nodiscard]] static evp_pkey generate(const std::string &algorithm_name,
                                         std::size_t bits);

  [[nodiscard]] static std::string export_private_pem(const evp_pkey &key);
  [[nodiscard]] static std::string export_public_pem(const evp_pkey &key);

  [[nodiscard]] static evp_pkey import_private_pem(std::string_view pem);
  [[nodiscard]] static evp_pkey import_public_pem(std::string_view pem);

  friend util::dynamic_byte_buffer evp_key_encrypt(util::const_byte_span data,
                                                   const evp_pkey &key,
                                                   encryption_padding padding);
  friend util::dynamic_byte_buffer evp_key_decrypt(util::const_byte_span data,
                                                   const evp_pkey &key,
                                                   encryption_padding padding);

private:
  struct native_helper;
  // should not be declared final as this prevents optimization for empty
  // deleter in std::unique_ptr
  struct evp_pkey_deleter {
    void operator()(void *evp_pkey) const noexcept;
  };

  using impl_ptr = std::unique_ptr<void, evp_pkey_deleter>;
  impl_ptr impl_{};
  bool private_{false};

  evp_pkey(void *native_ptr, bool has_private_component) noexcept
      : impl_{native_ptr}, private_{has_private_component} {}

  [[nodiscard]] std::string_view get_type_name_internal() const noexcept;
  void validate_algorithm_internal() const;
};

[[nodiscard]] util::dynamic_byte_buffer
evp_key_encrypt(util::const_byte_span data, const evp_pkey &key,
                encryption_padding padding);
[[nodiscard]] util::dynamic_byte_buffer
evp_key_decrypt(util::const_byte_span data, const evp_pkey &key,
                encryption_padding padding);

} // namespace opensslpp

#endif
