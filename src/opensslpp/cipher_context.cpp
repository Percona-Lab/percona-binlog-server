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

#include "opensslpp/cipher_context.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/algorithm/string/case_conv.hpp>

#include <boost/container/container_fwd.hpp>
#include <boost/container/static_vector.hpp>

#include <boost/endian/conversion.hpp>

#include <openssl/evp.h>
#include <openssl/types.h>

#include "opensslpp/cipher_mode_type.hpp"
#include "opensslpp/core_error.hpp"

#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace opensslpp {

struct cipher_context::native_helper {
  [[nodiscard]] static auto deimpl(auto &impl) noexcept {
    using cast_type = std::conditional_t<
        std::is_const_v<std::remove_reference_t<decltype(impl)>>,
        const EVP_CIPHER_CTX, EVP_CIPHER_CTX>;
    return static_cast<cast_type *>(impl.get());
  }

  [[nodiscard]] static const auto *
  get_cipher_by_name_internal(const std::string &cipher_name) noexcept {
    return EVP_get_cipherbyname(cipher_name.c_str());
  }
  [[nodiscard]] static cipher_mode_type
  convert_mode_internal(int native_mode) noexcept {
    switch (native_mode) {
    case EVP_CIPH_ECB_MODE:
      return cipher_mode_type::ecb;
    case EVP_CIPH_CBC_MODE:
      return cipher_mode_type::cbc;
    case EVP_CIPH_CFB_MODE:
      return cipher_mode_type::cfb;
    case EVP_CIPH_OFB_MODE:
      return cipher_mode_type::ofb;
    case EVP_CIPH_CTR_MODE:
      return cipher_mode_type::ctr;
    case EVP_CIPH_GCM_MODE:
      return cipher_mode_type::gcm;
    case EVP_CIPH_CCM_MODE:
      return cipher_mode_type::ccm;
    case EVP_CIPH_XTS_MODE:
      return cipher_mode_type::xts;
    case EVP_CIPH_WRAP_MODE:
      return cipher_mode_type::wrap;
    case EVP_CIPH_OCB_MODE:
      return cipher_mode_type::ocb;
    case EVP_CIPH_SIV_MODE:
      return cipher_mode_type::siv;
    default:
      return cipher_mode_type::delimiter;
    }
  }

  [[nodiscard]] static const auto *
  get_validated_cipher_by_name_internal(const std::string &cipher_name) {
    const auto *cipher{get_cipher_by_name_internal(cipher_name)};
    if (cipher == nullptr) {
      util::exception_location().raise<core_error>("unknown cipher name");
    }
    return cipher;
  }

  [[nodiscard]] static std::size_t
  get_block_size_in_bytes_internal(const EVP_CIPHER *cipher) noexcept {
    assert(cipher != nullptr);
    return static_cast<std::size_t>(EVP_CIPHER_get_block_size(cipher));
  }
  [[nodiscard]] static std::size_t
  get_key_size_in_bytes_internal(const EVP_CIPHER *cipher) noexcept {
    assert(cipher != nullptr);
    return static_cast<std::size_t>(EVP_CIPHER_get_key_length(cipher));
  }
  [[nodiscard]] static std::size_t
  get_iv_size_in_bytes_internal(const EVP_CIPHER *cipher) noexcept {
    assert(cipher != nullptr);
    return static_cast<std::size_t>(EVP_CIPHER_get_iv_length(cipher));
  }
};

void cipher_context::impl_deleter::operator()(void *cipher_ctx) const noexcept {
  if (cipher_ctx != nullptr) {
    EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX *>(cipher_ctx));
  }
}

cipher_context::cipher_context(cipher_context_operation_type operation,
                               const std::string &cipher_name,
                               util::const_byte_span key,
                               util::const_byte_span ivec,
                               util::const_byte_span tag)
    : impl_{EVP_CIPHER_CTX_new()} {
  if (!impl_) {
    util::exception_location().raise<core_error>(
        "cannot create cipher context");
  }
  const auto *evp_cipher{
      native_helper::get_validated_cipher_by_name_internal(cipher_name)};
  const auto mode{
      native_helper::convert_mode_internal(EVP_CIPHER_get_mode(evp_cipher))};
  if (!is_mode_supported(mode)) {
    util::exception_location().raise<core_error>("unsupported cipher mode");
  }

  if (std::size(key) !=
      native_helper::get_key_size_in_bytes_internal(evp_cipher)) {
    util::exception_location().raise<core_error>(
        "invalid key size for the specified cipher");
  }
  if (std::size(ivec) !=
      native_helper::get_iv_size_in_bytes_internal(evp_cipher)) {
    util::exception_location().raise<core_error>(
        "invalid iv size for the specified cipher");
  }
  if (operation == cipher_context_operation_type::encryption) {
    if (!tag.empty()) {
      util::exception_location().raise<core_error>(
          "tag must not be specified for encryption cipher context");
    }
  }

  if (EVP_CipherInit_ex(
          native_helper::deimpl(impl_), // context
          evp_cipher,                   // cipher
          nullptr,                      // engine
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<const unsigned char *>(std::data(key)), // key
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<const unsigned char *>(std::data(ivec)), // iv
          (operation == cipher_context_operation_type::encryption ? 1
                                                                  : 0) // enc
          ) == 0) {
    util::exception_location().raise<core_error>(
        "cannot initialize cipher context");
  }

  if (EVP_CIPHER_CTX_set_padding(native_helper::deimpl(impl_), 0) == 0) {
    util::exception_location().raise<core_error>(
        "cannot disable padding for cipher context");
  }

  if (operation == cipher_context_operation_type::decryption) {
    if (get_tag_size_in_bytes() != std::size(tag)) {
      util::exception_location().raise<core_error>(
          "invalid tag size for the specified cipher");
    }
    void *tag_ptr{
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        const_cast<void *>(static_cast<const void *>(std::data(tag)))};
    if (EVP_CIPHER_CTX_ctrl(native_helper::deimpl(impl_),     // context
                            EVP_CTRL_GCM_SET_TAG,             // type
                            static_cast<int>(std::size(tag)), // length
                            tag_ptr                           // tag
                            ) == 0) {
      util::exception_location().raise<core_error>(
          "cannot set tag for cipher context");
    }
  }
}

[[nodiscard]] cipher_context_operation_type
cipher_context::get_operation() const noexcept {
  assert(!is_empty());
  return (EVP_CIPHER_CTX_encrypting(native_helper::deimpl(impl_))
              ? cipher_context_operation_type::encryption
              : cipher_context_operation_type::decryption);
}

[[nodiscard]] cipher_mode_type cipher_context::get_mode() const noexcept {
  assert(!is_empty());
  return native_helper::convert_mode_internal(
      EVP_CIPHER_CTX_get_mode(native_helper::deimpl(impl_)));
}

[[nodiscard]] std::size_t
cipher_context::get_block_size_in_bytes() const noexcept {
  assert(!is_empty());
  return static_cast<std::size_t>(
      EVP_CIPHER_CTX_get_block_size(native_helper::deimpl(impl_)));
}

[[nodiscard]] std::size_t
cipher_context::get_key_size_in_bytes() const noexcept {
  assert(!is_empty());
  return static_cast<std::size_t>(
      EVP_CIPHER_CTX_get_key_length(native_helper::deimpl(impl_)));
}

[[nodiscard]] std::size_t
cipher_context::get_iv_size_in_bytes() const noexcept {
  assert(!is_empty());
  return static_cast<std::size_t>(
      EVP_CIPHER_CTX_get_iv_length(native_helper::deimpl(impl_)));
}

[[nodiscard]] std::size_t
cipher_context::get_tag_size_in_bytes() const noexcept {
  assert(!is_empty());
  return static_cast<std::size_t>(
      EVP_CIPHER_CTX_get_tag_length(native_helper::deimpl(impl_)));
}

[[nodiscard]] bool
cipher_context::is_cipher_name_known(const std::string &cipher_name) noexcept {
  return native_helper::get_cipher_by_name_internal(cipher_name) != nullptr;
}

[[nodiscard]] bool
cipher_context::is_mode_supported(cipher_mode_type mode) noexcept {
  switch (mode) {
  case cipher_mode_type::ecb:
  case cipher_mode_type::cbc:
  case cipher_mode_type::ctr:
  case cipher_mode_type::gcm:
    return true;
  default:
    // cipher_mode_type::cfb:
    // cipher_mode_type::ofb:
    // cipher_mode_type::ccm:
    // cipher_mode_type::xts:
    // cipher_mode_type::wrap:
    // cipher_mode_type::ocb:
    // cipher_mode_type::siv:
    return false;
  }
}

[[nodiscard]] bool cipher_context::is_cipher_name_supported(
    const std::string &cipher_name) noexcept {
  const auto *evp_cipher{
      native_helper::get_cipher_by_name_internal(cipher_name)};
  if (evp_cipher == nullptr) {
    return false;
  }
  const auto mode{
      native_helper::convert_mode_internal(EVP_CIPHER_get_mode(evp_cipher))};
  return is_mode_supported(mode);
}

[[nodiscard]] cipher_mode_type
cipher_context::get_mode(const std::string &cipher_name) noexcept {
  const auto *evp_cipher{
      native_helper::get_cipher_by_name_internal(cipher_name)};
  if (evp_cipher == nullptr) {
    return cipher_mode_type::delimiter;
  }
  return native_helper::convert_mode_internal(EVP_CIPHER_get_mode(evp_cipher));
}

[[nodiscard]] std::size_t
cipher_context::get_block_size_in_bytes(const std::string &cipher_name) {
  return native_helper::get_block_size_in_bytes_internal(
      native_helper::get_validated_cipher_by_name_internal(cipher_name));
}

[[nodiscard]] std::size_t
cipher_context::get_key_size_in_bytes(const std::string &cipher_name) {
  return native_helper::get_key_size_in_bytes_internal(
      native_helper::get_validated_cipher_by_name_internal(cipher_name));
}

[[nodiscard]] std::size_t
cipher_context::get_iv_size_in_bytes(const std::string &cipher_name) {
  return native_helper::get_iv_size_in_bytes_internal(
      native_helper::get_validated_cipher_by_name_internal(cipher_name));
}

void cipher_context::extract_updated_iv(util::byte_span ivec) {
  assert(!is_empty());
  if (std::size(ivec) != get_iv_size_in_bytes()) {
    util::exception_location().raise<core_error>(
        "in cipher context invalid buffer size for extracting updated iv");
  }

  if (!std::in_range<int>(std::size(ivec))) {
    util::exception_location().raise<core_error>(
        "in cipher context buffer size is out of range for extracting updated "
        "iv");
  }
  if (EVP_CIPHER_CTX_get_updated_iv(
          native_helper::deimpl(impl_),
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<unsigned char *>(std::data(ivec)),
          std::size(ivec)) == 0) {
    util::exception_location().raise<core_error>(
        "cannot get updated iv for cipher context");
  }
}

void cipher_context::update(util::const_byte_span input,
                            util::byte_span output) {
  assert(!is_empty());

  if (std::size(input) % get_block_size_in_bytes() != 0U) {
    util::exception_location().raise<core_error>(
        "in cipher context update input size is not a multiple of the block "
        "size");
  }

  // in all modes supported by us ('XXX-ECB' with padding disabled, 'XXX-CBC'
  // with padding disabled, 'XXX-CTR', and'XXX-GCM') the output length needs to
  // be of the same size as the input
  if (std::size(output) != std::size(input)) {
    util::exception_location().raise<core_error>(
        "in cipher context update the output size does not match the input "
        "size");
  }

  if (!std::in_range<int>(std::size(input))) {
    util::exception_location().raise<core_error>(
        "in cipher context update input size is out of range");
  }
  const auto input_size_native{static_cast<int>(std::size(input))};
  int output_size_native{0};
  if (EVP_CipherUpdate(
          native_helper::deimpl(impl_), // context
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<unsigned char *>(std::data(output)), // output
          &output_size_native,                                  // output length
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<const unsigned char *>(std::data(input)), // input
          input_size_native // input length
          ) == 0) {
    util::exception_location().raise<core_error>(
        "cannot update cipher context");
  }
  if (!std::in_range<std::size_t>(output_size_native)) {
    util::exception_location().raise<core_error>(
        "in cipher context update output size is out of range");
  }
  const auto output_size{static_cast<std::size_t>(output_size_native)};
  if (output_size != std::size(output)) {
    util::exception_location().raise<core_error>(
        "in cipher context update the actual output size does not match the "
        "expected output size");
  }
}

void cipher_context::finalize(util::byte_span output_tag) {
  assert(!is_empty());

  const auto operation{get_operation()};
  if (operation == cipher_context_operation_type::decryption) {
    if (!output_tag.empty()) {
      util::exception_location().raise<core_error>(
          "in cipher context finalize the output tag must only be specified "
          "for the encryption mode");
    }
  } else {
    // cipher_context_operation_type::encryption operation
    if (std::size(output_tag) != get_tag_size_in_bytes()) {
      util::exception_location().raise<core_error>(
          "in cipher context finalize the output tag size does not match the "
          "expected tag size");
    }
  }

  using fake_buffer_type = std::array<std::byte, EVP_MAX_BLOCK_LENGTH>;
  fake_buffer_type fake_buffer;
  int output_size_native{0};
  if (EVP_CipherFinal_ex(
          native_helper::deimpl(impl_),
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<unsigned char *>(std::data(fake_buffer)),
          &output_size_native) == 0) {
    util::exception_location().raise<core_error>(
        "cannot finalize cipher context");
  }
  if (!std::in_range<std::size_t>(output_size_native)) {
    util::exception_location().raise<core_error>(
        "in cipher context finalize output size is out of range");
  }
  const auto output_size{static_cast<std::size_t>(output_size_native)};
  if (output_size != 0U) {
    util::exception_location().raise<core_error>(
        "in cipher context finalize the actual output size is not zero");
  }

  if (operation == cipher_context_operation_type::encryption) {
    const auto tag_size_native{static_cast<int>(std::size(output_tag))};
    void *const tag_ptr{
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        const_cast<void *>(static_cast<const void *>(std::data(output_tag)))};
    if (EVP_CIPHER_CTX_ctrl(native_helper::deimpl(impl_), // context
                            EVP_CTRL_GCM_GET_TAG,         // type
                            tag_size_native,              // length
                            tag_ptr                       // tag
                            ) == 0) {
      util::exception_location().raise<core_error>(
          "cannot get tag from cipher context");
    }
  }
  impl_.reset();
}

cipher_context cipher_context::create_with_offset(
    std::uint64_t offset, cipher_context_operation_type operation,
    const std::string &cipher_name, util::const_byte_span key,
    util::const_byte_span ivec, util::const_byte_span tag) {
  if (offset == 0ULL) {
    // early return when offset is zero to avoid unnecessary copying
    return cipher_context{operation, cipher_name, key, ivec, tag};
  }

  static constexpr cipher_mode_type expected_mode{cipher_mode_type::ctr};
  static constexpr cipher_mode_type non_streaming_mode{cipher_mode_type::ecb};

  if (get_mode(cipher_name) != expected_mode) {
    util::exception_location().raise<core_error>(
        "in cipher context creation with offset the specified cipher is not a "
        "CTR cipher");
  }
  if (std::size(ivec) != get_iv_size_in_bytes(cipher_name)) {
    util::exception_location().raise<core_error>(
        "in cipher context creation with offset the size of the iv does not "
        "match the expected iv size for the specified cipher");
  }
  if (std::size(ivec) < sizeof(std::uint64_t)) {
    util::exception_location().raise<core_error>(
        "in cipher context creation with offset the size of the iv is too "
        "small");
  }

  const std::byte *original_ivec_ptr{std::data(ivec)};
  std::advance(original_ivec_ptr, std::size(ivec) - sizeof(std::uint64_t));
  std::uint64_t counter{boost::endian::load_big_u64(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<const unsigned char *>(original_ivec_ptr))};

  // we cannot use get_block_size_in_bytes() because it returns 1 for CTR and
  // GCM

  // a helper lambda that modifies the mode part of the provided cipher name
  // from expected 'expected_mode' ('CTR') to 'non_streaming_mode' ('ECB')
  // preserving the case
  const auto cipher_name_mofifier{[](std::string &inplace_cipher_name) -> bool {
    static constexpr char delimiter{'-'};
    const auto pos{inplace_cipher_name.rfind(delimiter)};
    if (pos == std::string::npos) {
      return false;
    }
    const auto extracted_mode_str{inplace_cipher_name.substr(pos + 1)};
    std::string expected_mode_str{to_string_view(expected_mode)};
    bool expected_mode_found{false};
    bool upper_case_extracted_mode{false};
    if (extracted_mode_str == expected_mode_str) {
      expected_mode_found = true;
    } else {
      boost::algorithm::to_upper(expected_mode_str);
      if (extracted_mode_str == expected_mode_str) {
        expected_mode_found = true;
        upper_case_extracted_mode = true;
      }
    }
    if (!expected_mode_found) {
      return false;
    }
    inplace_cipher_name.resize(pos + 1);
    std::string new_mode{to_string_view(non_streaming_mode)};
    if (upper_case_extracted_mode) {
      boost::algorithm::to_upper(new_mode);
    }
    inplace_cipher_name += new_mode;
    return true;
  }};
  std::string modified_cipher_name{cipher_name};
  if (!cipher_name_mofifier(modified_cipher_name)) {
    util::exception_location().raise<core_error>(
        "in cipher context creation with offset the specified cipher name does "
        "not have the expected format");
  }
  const std::uint64_t block_size{get_block_size_in_bytes(modified_cipher_name)};
  counter += offset / block_size;

  using buffer_type = boost::container::static_vector<
      std::byte, std::max(EVP_MAX_IV_LENGTH, EVP_MAX_BLOCK_LENGTH)>;
  buffer_type modified_ivec{std::size(ivec)};
  auto *dest_ptr{std::data(modified_ivec)};

  const auto *source_en{std::data(ivec)};
  std::advance(source_en, std::size(ivec) - sizeof(std::uint64_t));
  dest_ptr = std::copy(std::data(ivec), source_en, dest_ptr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  boost::endian::store_big_u64(reinterpret_cast<unsigned char *>(dest_ptr),
                               counter);

  cipher_context result{operation, cipher_name, key, modified_ivec, tag};

  // TODO: consider using EVP_CIPHER_CTX_set_num() instead of a dummy block
  //       trick
  buffer_type source_dummy_block{offset % block_size,
                                 boost::container::default_init};
  buffer_type dest_dummy_block{offset % block_size};
  result.update(source_dummy_block, dest_dummy_block);

  return result;
}

} // namespace opensslpp
