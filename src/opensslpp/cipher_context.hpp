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

#ifndef OPENSSLPP_CIPHER_CONTEXT_HPP
#define OPENSSLPP_CIPHER_CONTEXT_HPP

#include "opensslpp/cipher_context_fwd.hpp" // IWYU pragma: export

#include <memory>
#include <string>

#include "opensslpp/cipher_mode_type_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace opensslpp {

class cipher_context {
public:
  cipher_context() noexcept = default;
  // * 'operation' must be either cipher_context_operation_type::encryption or
  //   cipher_context_operation_type::decryption
  // * 'cipher_name' must be a valid cipher name supported by OpenSSL
  //   (currently only 'XXX-ECB', 'XXX-CBC', 'XXX-CTR', and'XXX-GCM')
  // * 'key' must be of proper length for the given cipher (see
  //   get_key_size_in_bytes())
  // * 'ivec' must be of proper length for the given cipher (see
  //   get_iv_size_in_bytes())
  //   ('ivec' is not needed and can be empty for 'XXX-ECB' ciphers)
  // * 'tag' is only used for 'XXX-GCM' ciphers and must be of proper length
  //   for the given cipher (see get_tag_size_in_bytes())
  //
  // For 'XXX-ECB' and 'XXX-CBC' we deliberately disable padding as it cannot be
  // considered as a reliable way of restoring original data of lengths that are
  // not multiples of the cipher's block size. In these modes the 'update()'
  // method expects the input to be of length that is a multiple of the cipher's
  // block size.
  cipher_context(
      // no std::string_view for 'cipher' as it needs to be null-terminated
      cipher_context_operation_type operation, const std::string &cipher_name,
      util::const_byte_span key, util::const_byte_span ivec = {},
      util::const_byte_span tag = {});
  ~cipher_context() noexcept = default;

  cipher_context(const cipher_context &obj) = delete;
  cipher_context(cipher_context &&obj) noexcept = default;

  cipher_context &operator=(const cipher_context &obj) = delete;
  cipher_context &operator=(cipher_context &&obj) noexcept = default;

  void swap(cipher_context &obj) noexcept { impl_.swap(obj.impl_); }

  [[nodiscard]] bool is_empty() const noexcept { return !impl_; }

  [[nodiscard]] cipher_context_operation_type get_operation() const noexcept;

  [[nodiscard]] cipher_mode_type get_mode() const noexcept;
  [[nodiscard]] std::size_t get_block_size_in_bytes() const noexcept;
  [[nodiscard]] std::size_t get_key_size_in_bytes() const noexcept;
  [[nodiscard]] std::size_t get_iv_size_in_bytes() const noexcept;
  [[nodiscard]] std::size_t get_tag_size_in_bytes() const noexcept;

  [[nodiscard]] static bool
  is_cipher_name_known(const std::string &cipher_name) noexcept;
  [[nodiscard]] static bool is_mode_supported(cipher_mode_type mode) noexcept;
  [[nodiscard]] static bool
  is_cipher_name_supported(const std::string &cipher_name) noexcept;
  [[nodiscard]] static cipher_mode_type
  get_mode(const std::string &cipher_name) noexcept;
  [[nodiscard]] static std::size_t
  get_block_size_in_bytes(const std::string &cipher_name);
  [[nodiscard]] static std::size_t
  get_key_size_in_bytes(const std::string &cipher_name);
  [[nodiscard]] static std::size_t
  get_iv_size_in_bytes(const std::string &cipher_name);
  // there is no static version of get_tag_size_in_bytes() as tag size is a
  // dynamic property of the cipher context

  // this is not a const method as underlying EVP_CIPHER_CTX_get_updated_iv()
  // accepts non-const EVP_CIPHER_CTX pointer
  void extract_updated_iv(util::byte_span ivec);

  // TODO: implement void update_inplace(util::byte_span inoutput)
  void update(util::const_byte_span input, util::byte_span output);
  void finalize(util::byte_span output_tag = {});

  static cipher_context create_with_offset(
      std::uint64_t offset, cipher_context_operation_type operation,
      const std::string &cipher_name, util::const_byte_span key,
      util::const_byte_span ivec = {}, util::const_byte_span tag = {});

private:
  struct native_helper;
  struct impl_deleter {
    void operator()(void *cipher_ctx) const noexcept;
  };

  using impl_ptr = std::unique_ptr<void, impl_deleter>;
  impl_ptr impl_;
};

} // namespace opensslpp

#endif // OPENSSLPP_CIPHER_CONTEXT_HPP
