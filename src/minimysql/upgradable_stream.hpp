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

#ifndef MINIMYSQL_UPGRADABLE_STREAM_HPP
#define MINIMYSQL_UPGRADABLE_STREAM_HPP

#include "minimysql/upgradable_stream_fwd.hpp" // IWYU pragma: export

#include <cassert>
#include <stdexcept>
#include <utility>
#include <variant>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"

#include <boost/asio/async_result.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>

#pragma GCC diagnostic pop

#include <boost/system/error_code.hpp> // IWYU pragma: keep

#include "util/exception_location_helpers.hpp"

namespace minimysql {

class upgradable_stream {
public:
  using underlying_stream_type =
      boost::asio::basic_stream_socket<boost::asio::ip::tcp>;
  using secure_stream_type = boost::asio::ssl::stream<underlying_stream_type>;
  using executor_type = secure_stream_type::executor_type;

  explicit upgradable_stream(underlying_stream_type underlying_stream)
      : stream_{std::move(underlying_stream)} {}

  upgradable_stream(const upgradable_stream &) = delete;
  upgradable_stream(upgradable_stream &&) = default;
  upgradable_stream &operator=(const upgradable_stream &) = delete;
  upgradable_stream &operator=(upgradable_stream &&) = default;

  ~upgradable_stream() = default;

  [[nodiscard]] bool is_secure() const noexcept {
    return std::holds_alternative<secure_stream_type>(stream_);
  }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"

  [[nodiscard]] const underlying_stream_type &
  get_underlying_stream() const noexcept {
    if (const auto *secure_stream{std::get_if<secure_stream_type>(&stream_)};
        secure_stream != nullptr) {
      return secure_stream->next_layer();
    }
    const auto *underlying_stream{
        std::get_if<underlying_stream_type>(&stream_)};
    assert(underlying_stream != nullptr);
    return *underlying_stream;
  }
  [[nodiscard]] underlying_stream_type &get_underlying_stream() noexcept {
    if (auto *secure_stream{std::get_if<secure_stream_type>(&stream_)};
        secure_stream != nullptr) {
      return secure_stream->next_layer();
    }
    auto *underlying_stream{std::get_if<underlying_stream_type>(&stream_)};
    assert(underlying_stream != nullptr);
    return *underlying_stream;
  }

#pragma GCC diagnostic pop

  [[nodiscard]] executor_type get_executor() noexcept {
    return get_underlying_stream().get_executor();
  }

  // asynchronously upgrade the stream to a secure SSL/TLS connection
  template <boost::asio::completion_token_for<void(boost::system::error_code)>
                HandshakeToken =
                    boost::asio::default_completion_token_t<executor_type>>
  auto async_upgrade_to_secure(
      boost::asio::ssl::context &context,
      boost::asio::ssl::stream_base::handshake_type role,
      HandshakeToken &&token =
          boost::asio::default_completion_token_t<executor_type>{}) {
    auto *underlying_stream{std::get_if<underlying_stream_type>(&stream_)};
    if (underlying_stream == nullptr) {
      util::exception_location().raise<std::logic_error>(
          "stream has already been upgraded to secure");
    }
    auto extracted_underlying{std::move(*underlying_stream)};
    auto &secure_stream{stream_.emplace<secure_stream_type>(
        std::move(extracted_underlying), context)};
    return secure_stream.async_handshake(role,
                                         std::forward<HandshakeToken>(token));
  }

  // asynchronously shut down the secure SSL/TLS connection if it is active
  // throws otherwise
  template <boost::asio::completion_token_for<void(boost::system::error_code)>
                ShutdownToken =
                    boost::asio::default_completion_token_t<executor_type>>
  auto async_secure_shutdown(
      ShutdownToken &&token =
          boost::asio::default_completion_token_t<executor_type>()) {
    auto *secure_stream{std::get_if<secure_stream_type>(&stream_)};
    if (secure_stream == nullptr) {
      util::exception_location().raise<std::logic_error>(
          "cannot call async_shutdown on non-secure stream");
    }
    return secure_stream->async_shutdown(std::forward<ShutdownToken>(token));
  }

  template <typename ConstBufferSequence,
            boost::asio::completion_token_for<void(boost::system::error_code,
                                                   std::size_t)>
                WriteToken =
                    boost::asio::default_completion_token_t<executor_type>>
  auto async_write_some(
      const ConstBufferSequence &buffers,
      WriteToken &&token =
          boost::asio::default_completion_token_t<executor_type>()) {
    return std::visit(
        [&buffers, &token](auto &stream) {
          return stream.async_write_some(buffers,
                                         std::forward<WriteToken>(token));
        },
        stream_);
  }

  template <typename MutableBufferSequence,
            boost::asio::completion_token_for<void(boost::system::error_code,
                                                   std::size_t)>
                ReadToken =
                    boost::asio::default_completion_token_t<executor_type>>
  auto async_read_some(
      const MutableBufferSequence &buffers,
      ReadToken &&token =
          boost::asio::default_completion_token_t<executor_type>()) {
    return std::visit(
        [&buffers, &token](auto &stream) {
          return stream.async_read_some(buffers,
                                        std::forward<ReadToken>(token));
        },
        stream_);
  }

private:
  using stream_variant =
      std::variant<underlying_stream_type, secure_stream_type>;
  stream_variant stream_;
};

} // namespace minimysql

#endif // MINIMYSQL_UPGRADABLE_STREAM_HPP
