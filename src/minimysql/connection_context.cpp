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

#include "minimysql/connection_context.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include <boost/asio/buffer.hpp>

#include <boost/system/system_error.hpp>

#include <mysql/mysqld_error.h>

// code included from MySQL Router's classic protocol codec implementation
// has a number of conversion warnings, so we disable them before inclusion

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"

#include "mysqlrouter/classic_protocol_codec_base.h"
#include "mysqlrouter/classic_protocol_codec_frame.h"
#include "mysqlrouter/classic_protocol_codec_message.h"
#include "mysqlrouter/classic_protocol_constants.h"
#include "mysqlrouter/classic_protocol_frame.h"
#include "mysqlrouter/classic_protocol_message.h"
#include "mysqlrouter/classic_protocol_wire.h"

#include "mysql/harness/stdx/flags.h"

#pragma GCC diagnostic pop

#include "minimysql/caching_sha2_password_authenticator.hpp"

namespace minimysql {

namespace {

template <class MessageType>
classic_protocol::frame::Frame<MessageType>
decode_client_command_frame(const connection_buffer_type &payload,
                            const capability_bitset &shared_capabilities) {
  auto buffer{boost::asio::buffer(payload)};
  using frame_type = classic_protocol::frame::Frame<MessageType>;

  auto decode_result{
      classic_protocol::decode<frame_type>(buffer, shared_capabilities)};
  if (!decode_result) {
    throw boost::system::system_error{decode_result.error()};
  }

  return decode_result.value().second;
}

} // namespace

connection_context::connection_context(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view server_username, std::string_view server_password)
    : server_username_(server_username), server_password_(server_password),
      connection_id_(next_connection_id_++) {
  static_assert(std::is_same_v<capability_bitset,
                               classic_protocol::capabilities::value_type>,
                "capability_bitset MUST be the same type as "
                "classic_protocol::capabilities::value_type");
}

[[nodiscard]] bool connection_context::check_client_authentication() const {
  return get_client_username() == get_server_username() &&
         get_client_auth_method_data() ==
             caching_sha2_password_authenticator::scramble(
                 get_server_password(), get_server_auth_method_data());
}

[[nodiscard]] bool
connection_context::check_shared_plugin_auth_supported() const {
  // this method is not noexcept as it used std::bitset<>::test() which is
  // not noexcept
  return get_shared_capabilities().test(
      classic_protocol::capabilities::pos::plugin_auth);
}

[[nodiscard]] bool
connection_context::check_shared_text_result_with_session_tracking_supported()
    const {
  // this method is not noexcept as it used std::bitset<>::test() which is
  // not noexcept
  return get_shared_capabilities().test(
      classic_protocol::capabilities::pos::text_result_with_session_tracking);
}

[[nodiscard]] bool
connection_context::check_binlog_non_blocking_dump() const noexcept {
  stdx::flags<classic_protocol::message::client::BinlogDump::Flags>
      binlog_dump_flags{};
  binlog_dump_flags.underlying_value(get_binlog_flags());
  return (binlog_dump_flags &
          classic_protocol::message::client::BinlogDump::Flags::non_blocking);
}

[[nodiscard]] std::size_t
connection_context::get_frame_header_length() noexcept {
  return classic_protocol::Codec<classic_protocol::frame::Header>::max_size();
}

[[nodiscard]] std::size_t
connection_context::parse_frame_header(const connection_buffer_type &payload) {

  auto buffer{boost::asio::buffer(payload)};
  auto decode_result{
      classic_protocol::decode<classic_protocol::frame::Header>(buffer, {})};
  if (!decode_result) {
    throw boost::system::system_error{decode_result.error()};
  }

  return decode_result.value().second.payload_size();
}

[[nodiscard]] std::string
connection_context::generate_encoded_server_greeting() {
  std::string result_buffer{};

  server_capabilities_ = get_default_server_capabilities();
  server_auth_method_ = std::string{default_server_auth_method};

  // for historical reasons sever auth data must include a trailing '\0' byte
  const std::string fixed_server_auth_data{generate_server_auth_method_data() +
                                           '\0'};

  const classic_protocol::message::server::Greeting server_greeting{
      default_server_protocol_version,
      std::string{default_server_version},
      get_connection_id(),
      fixed_server_auth_data,
      get_server_capabilities(),
      default_server_collation,
      default_server_status_flags,
      get_server_auth_method()};

  using server_greeting_frame = classic_protocol::frame::Frame<
      classic_protocol::message::server::Greeting>;
  auto encode_result{classic_protocol::encode<server_greeting_frame>(
      {generate_sequence_number(), server_greeting}, get_server_capabilities(),
      boost::asio::dynamic_buffer(result_buffer))};

  if (!encode_result) {
    throw boost::system::system_error{encode_result.error()};
  }
  return result_buffer;
}

void connection_context::parse_client_greeting(
    const connection_buffer_type &payload) {
  auto buffer{boost::asio::buffer(payload)};
  using client_greeting_frame = classic_protocol::frame::Frame<
      classic_protocol::message::client::Greeting>;
  auto decode_result{classic_protocol::decode<client_greeting_frame>(
      buffer, get_server_capabilities())};
  if (!decode_result) {
    throw boost::system::system_error{decode_result.error()};
  }

  validate_and_update_sequence_number(decode_result.value().second.seq_id());

  const auto &client_greeting{decode_result.value().second.payload()};

  client_capabilities_ = client_greeting.capabilities();
  client_auth_method_ = client_greeting.auth_method_name();
  client_auth_method_data_ = client_greeting.auth_method_data();
  client_username_ = client_greeting.username();
  client_schema_ = client_greeting.schema();
  client_collation_ = client_greeting.collation();
  client_max_packet_size_ = client_greeting.max_packet_size();
  client_attributes_ = client_greeting.attributes();
}

[[nodiscard]] connection_buffer_type
connection_context::generate_encoded_fast_auth() {
  std::string result_buffer{};

  static constexpr std::string_view fast_auth_code{
      "\x03"}; // 0x03 means "fast auth success" in caching_sha2_password
               // protocol
  using auth_method_data_frame = classic_protocol::frame::Frame<
      classic_protocol::message::server::AuthMethodData>;
  auto encode_res = classic_protocol::encode<auth_method_data_frame>(
      {generate_sequence_number(), {std::string{fast_auth_code}}},
      get_shared_capabilities(), boost::asio::dynamic_buffer(result_buffer));

  if (!encode_res) {
    throw boost::system::system_error{encode_res.error()};
  }
  return result_buffer;
}

[[nodiscard]] connection_buffer_type connection_context::generate_encoded_ok() {
  std::string result_buffer{};

  const classic_protocol::message::server::Ok ok_message{};

  using ok_frame =
      classic_protocol::frame::Frame<classic_protocol::message::server::Ok>;
  auto encode_result{classic_protocol::encode<ok_frame>(
      {generate_sequence_number(), ok_message}, get_shared_capabilities(),
      boost::asio::dynamic_buffer(result_buffer))};

  if (!encode_result) {
    throw boost::system::system_error{encode_result.error()};
  }
  return result_buffer;
}

[[nodiscard]] connection_buffer_type
connection_context::generate_encoded_eof() {
  std::string result_buffer{};

  const classic_protocol::message::server::Eof eof_message{};

  using eof_frame =
      classic_protocol::frame::Frame<classic_protocol::message::server::Eof>;
  auto encode_result{classic_protocol::encode<eof_frame>(
      {generate_sequence_number(), eof_message}, get_shared_capabilities(),
      boost::asio::dynamic_buffer(result_buffer))};

  if (!encode_result) {
    throw boost::system::system_error{encode_result.error()};
  }
  return result_buffer;
}

[[nodiscard]] connection_buffer_type connection_context::generate_encoded_error(
    std::uint16_t error_code,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view message, std::string_view sql_state) {
  std::string result_buffer{};

  classic_protocol::message::server::Error error_message{};
  error_message.error_code(error_code);
  error_message.message(std::string{message});
  error_message.sql_state(std::string{sql_state});

  using error_frame =
      classic_protocol::frame::Frame<classic_protocol::message::server::Error>;
  auto encode_result{classic_protocol::encode<error_frame>(
      {generate_sequence_number(), error_message}, get_shared_capabilities(),
      boost::asio::dynamic_buffer(result_buffer))};

  if (!encode_result) {
    throw boost::system::system_error{encode_result.error()};
  }
  return result_buffer;
}

[[nodiscard]] connection_buffer_type
connection_context::generate_encoded_access_denied() {
  return generate_encoded_error(
      ER_ACCESS_DENIED_ERROR,
      "Access Denied for user '" + get_client_username() + "'@'%'", "28000");
}
[[nodiscard]] connection_buffer_type
connection_context::generate_encoded_unknown_command() {
  return generate_encoded_error(ER_UNKNOWN_COM_ERROR, "Unknown command",
                                "HY000");
}
[[nodiscard]] connection_buffer_type
connection_context::generate_encoded_syntax_error() {
  // 00000	Success
  // 01000	Warning
  // 02000	No data
  // 23000	Integrity constraint violation
  // 28000	Invalid authorization
  // 42000	Syntax error or access rule violation
  // HY000	General error (MySQL‑specific catch‑all)
  return generate_encoded_error(ER_PARSE_ERROR, "Syntax error", "42000");
}

[[nodiscard]] connection_buffer_type
connection_context::generate_encoded_binlog_event(std::string_view event_data) {
  std::string result_buffer{};
  result_buffer.reserve(get_frame_header_length() + std::size(event_data) + 1U);

  auto encode_result{classic_protocol::encode<classic_protocol::frame::Header>(
      {std::size(event_data) + 1U, generate_sequence_number()},
      get_shared_capabilities(), boost::asio::dynamic_buffer(result_buffer))};

  if (!encode_result) {
    throw boost::system::system_error{encode_result.error()};
  }
  result_buffer +=
      '\0'; // the first byte in the payload must be '\0' to indicate OK
  result_buffer += event_data;
  return result_buffer;
}

void connection_context::parse_client_command(
    const connection_buffer_type &payload) {
  if (payload.size() <= get_frame_header_length()) {
    throw boost::system::system_error{
        make_error_code(std::errc::protocol_error)};
  }

  const auto command_byte_index{get_frame_header_length()};
  const auto command_byte{
      static_cast<std::uint8_t>(payload[command_byte_index])};

  switch (command_byte) {
  case classic_protocol::Codec<
      classic_protocol::message::client::Query>::cmd_byte(): {
    auto frame =
        decode_client_command_frame<classic_protocol::message::client::Query>(
            payload, get_shared_capabilities());
    validate_and_update_sequence_number(frame.seq_id());

    client_command_ = client_command_type::query;
    client_statement_ = frame.payload().statement();
    break;
  }
  case classic_protocol::Codec<
      classic_protocol::message::client::Quit>::cmd_byte(): {
    auto frame =
        decode_client_command_frame<classic_protocol::message::client::Quit>(
            payload, get_shared_capabilities());
    validate_and_update_sequence_number(frame.seq_id());

    client_command_ = client_command_type::quit;
    break;
  }
  case classic_protocol::Codec<
      classic_protocol::message::client::ResetConnection>::cmd_byte(): {
    auto frame = decode_client_command_frame<
        classic_protocol::message::client::ResetConnection>(
        payload, get_shared_capabilities());
    validate_and_update_sequence_number(frame.seq_id());

    client_command_ = client_command_type::reset_connection;
    break;
  }
  case classic_protocol::Codec<
      classic_protocol::message::client::ChangeUser>::cmd_byte(): {
    auto frame = decode_client_command_frame<
        classic_protocol::message::client::ChangeUser>(
        payload, get_shared_capabilities());
    validate_and_update_sequence_number(frame.seq_id());

    client_command_ = client_command_type::change_user;
    break;
  }
  case classic_protocol::Codec<
      classic_protocol::message::client::Ping>::cmd_byte(): {
    auto frame =
        decode_client_command_frame<classic_protocol::message::client::Ping>(
            payload, get_shared_capabilities());
    validate_and_update_sequence_number(frame.seq_id());

    client_command_ = client_command_type::ping;
    break;
  }
  case classic_protocol::Codec<
      classic_protocol::message::client::BinlogDump>::cmd_byte(): {
    auto frame = decode_client_command_frame<
        classic_protocol::message::client::BinlogDump>(
        payload, get_shared_capabilities());
    validate_and_update_sequence_number(frame.seq_id());

    client_command_ = client_command_type::binlog_dump;
    const auto &binlog_dump_payload{frame.payload()};
    binlog_flags_ = binlog_dump_payload.flags().underlying_value();
    binlog_server_id_ = binlog_dump_payload.server_id();
    binlog_filename_ = binlog_dump_payload.filename();
    binlog_position_ = binlog_dump_payload.position();
    break;
  }
  case classic_protocol::Codec<
      classic_protocol::message::client::BinlogDumpGtid>::cmd_byte(): {
    auto frame = decode_client_command_frame<
        classic_protocol::message::client::BinlogDumpGtid>(
        payload, get_shared_capabilities());
    validate_and_update_sequence_number(frame.seq_id());

    client_command_ = client_command_type::binlog_dump_gtid;
    break;
  }
  default:
    client_command_ = client_command_type::unknown;
  }
}

std::atomic_uint32_t connection_context::next_connection_id_{0U};

[[nodiscard]] capability_bitset
connection_context::get_default_server_capabilities() noexcept {
  return classic_protocol::capabilities::long_password |
         classic_protocol::capabilities::found_rows |
         classic_protocol::capabilities::long_flag |
         classic_protocol::capabilities::connect_with_schema |
         classic_protocol::capabilities::no_schema |
         // compress (not yet)
         classic_protocol::capabilities::odbc |
         classic_protocol::capabilities::local_files |
         // ignore_space (client only)
         classic_protocol::capabilities::protocol_41 |
         // interactive (client-only)
         // ssl (below)
         // ignore sigpipe (client-only)
         classic_protocol::capabilities::transactions |
         classic_protocol::capabilities::secure_connection |
         // multi_statements (not yet)
         classic_protocol::capabilities::multi_results |
         classic_protocol::capabilities::ps_multi_results |
         classic_protocol::capabilities::plugin_auth |
         classic_protocol::capabilities::connect_attributes |
         classic_protocol::capabilities::client_auth_method_data_varint
      // disable expired_passwords
      // disable session_track
      // disable text_result_with_session_tracking
      // optional_resultset_metadata (not yet)
      // compress_zstd (not yet)
      ;
}

[[nodiscard]] const std::string &
connection_context::generate_server_auth_method_data() {
  // TODO: generate random auth method data (for caching_sha2_password, it must
  // be 20 random bytes)
  server_auth_method_data_ = "01234567890123456789";
  return server_auth_method_data_;
}

[[nodiscard]] std::uint8_t connection_context::generate_sequence_number() {
  return sequence_number_++;
}

void connection_context::validate_and_update_sequence_number(
    std::uint8_t sequence_number) {
  if (sequence_number != sequence_number_) {
    throw boost::system::system_error{
        make_error_code(std::errc::protocol_error)};
  }
  ++sequence_number_;
}

void connection_context::encode_resultset_number_of_columns_internal(
    connection_buffer_container &result_buffers,
    std::size_t number_of_columns) {
  const classic_protocol::wire::VarInt column_count{
      static_cast<std::int64_t>(number_of_columns)};
  using column_count_frame =
      classic_protocol::frame::Frame<classic_protocol::wire::VarInt>;
  const auto encode_result = classic_protocol::encode<column_count_frame>(
      {generate_sequence_number(), column_count}, get_shared_capabilities(),
      boost::asio::dynamic_buffer(result_buffers.emplace_back()));
  if (!encode_result) {
    throw boost::system::system_error{encode_result.error()};
  }
}

struct connection_context::datatype_properties_type {
  classic_protocol::collation::value_type collation;
  std::uint32_t column_length;
  classic_protocol::field_type::value_type field_type;
  classic_protocol::column_def::value_type column_flags;
  std::uint8_t decimals;
};

template <typename Type> struct standard_type_translator {
  static constexpr connection_context::datatype_properties_type translate() {
    classic_protocol::collation::value_type collation{};
    if constexpr (std::is_integral_v<Type>) {
      collation = classic_protocol::collation::Binary;
    } else if constexpr (std::is_same_v<Type, std::string>) {
      collation = classic_protocol::collation::Latin1SwedishCi;
    }

    std::uint32_t column_length{};
    if constexpr (std::is_integral_v<Type>) {
      column_length = std::numeric_limits<Type>::digits10 + 1;
    } else if constexpr (std::is_same_v<Type, std::string>) {
      // just some default for string types, can be adjusted as needed
      constexpr std::size_t default_varchar_column_length{255U};
      column_length = default_varchar_column_length;
    }

    classic_protocol::field_type::value_type field_type{};
    if constexpr (std::is_integral_v<Type>) {
      field_type = classic_protocol::field_type::LongLong;
    } else if constexpr (std::is_same_v<Type, std::string>) {
      field_type = classic_protocol::field_type::VarString;
    }

    classic_protocol::column_def::value_type column_flags{};
    if constexpr (std::is_integral_v<Type>) {
      column_flags |= classic_protocol::column_def::numeric;
      if constexpr (std::is_unsigned_v<Type>) {
        column_flags |= classic_protocol::column_def::is_unsigned;
      }
    } else if constexpr (std::is_same_v<Type, std::string>) {
      // do nothing for string types
    }
    column_flags |= classic_protocol::column_def::not_null;

    std::uint8_t decimals{};
    if constexpr (std::is_integral_v<Type>) {
      // for integral types it should be 0
      decimals = 0U;
    } else if constexpr (std::is_same_v<Type, std::string>) {
      constexpr std::uint8_t magic_varchar_decimals{31U};
      decimals = magic_varchar_decimals;
    }
    return {.collation = collation,
            .column_length = column_length,
            .field_type = field_type,
            .column_flags = column_flags,
            .decimals = decimals};
  }
};

template <typename Type> struct standard_type_translator<std::optional<Type>> {
  static constexpr connection_context::datatype_properties_type
  translate() noexcept {
    auto result{standard_type_translator<Type>::translate()};
    // for optional types we can just use the same translation as for the
    // underlying type, but without the 'not_null' flag
    result.column_flags &= ~classic_protocol::column_def::not_null;
    return result;
  }
};

template <typename Type>
static constexpr connection_context::datatype_properties_type
    datatype_properties{standard_type_translator<Type>::translate()};

template <typename Type>
const connection_context::datatype_properties_type &
connection_context::get_datatype_properties() noexcept {
  return datatype_properties<Type>;
}

template const connection_context::datatype_properties_type &
connection_context::get_datatype_properties<std::string>() noexcept;
template const connection_context::datatype_properties_type &
connection_context::get_datatype_properties<
    std::optional<std::string>>() noexcept;
template const connection_context::datatype_properties_type &
connection_context::get_datatype_properties<std::uint64_t>() noexcept;
template const connection_context::datatype_properties_type &
connection_context::get_datatype_properties<
    std::optional<std::uint64_t>>() noexcept;

void connection_context::encode_resultset_column_definition_internal(
    connection_buffer_container &result_buffers, std::string_view db_name,
    std::string_view table_name, const column_name_pair &column_name,
    const datatype_properties_type &datatype_properties) {
  // 'binary' collation for integer types
  classic_protocol::message::server::ColumnMeta column_definition{
      "def" /* catalog */,
      std::string{db_name} /* schema */,
      std::string{table_name} /* table */,
      std::string{table_name} /* orig_table */,
      std::string{column_name.first} /* name */,
      std::string{column_name.second} /* orig_name */,
      datatype_properties.collation /* collation */,
      datatype_properties.column_length /* column_length */,
      datatype_properties.field_type /* type */,
      datatype_properties.column_flags /* flags */,
      datatype_properties.decimals /* decimals */
  };
  using column_definition_frame = classic_protocol::frame::Frame<
      classic_protocol::message::server::ColumnMeta>;
  const auto encode_result = classic_protocol::encode<column_definition_frame>(
      {generate_sequence_number(), std::move(column_definition)},
      get_shared_capabilities(),
      boost::asio::dynamic_buffer(result_buffers.emplace_back()));
  if (!encode_result) {
    throw boost::system::system_error{encode_result.error()};
  }
}

void connection_context::encode_resultset_intermediate_eof_internal(
    connection_buffer_container &result_buffers) {
  const auto encode_result = classic_protocol::encode<
      classic_protocol::frame::Frame<classic_protocol::message::server::Eof>>(
      {generate_sequence_number(), {}}, get_shared_capabilities(),
      boost::asio::dynamic_buffer(result_buffers.emplace_back()));
  if (!encode_result) {
    throw boost::system::system_error{encode_result.error()};
  }
}

void connection_context::encode_resultset_textualized_row_internal(
    connection_buffer_container &result_buffers,
    const optional_string_collection &row_values) {
  static_assert(
      std::is_same_v<optional_string,
                     classic_protocol::message::server::Row::value_type>);
  using row_frame =
      classic_protocol::frame::Frame<classic_protocol::message::server::Row>;
  const auto encode_result = classic_protocol::encode<row_frame>(
      {generate_sequence_number(), {row_values}}, get_shared_capabilities(),
      boost::asio::dynamic_buffer(result_buffers.emplace_back()));
  if (!encode_result) {
    throw boost::system::system_error{encode_result.error()};
  }
}

void connection_context::encode_resultset_eof_internal(
    connection_buffer_container &result_buffers) {
  classic_protocol::message::server::Eof eof_packet{};
  const classic_protocol::status::value_type eof_status_flags{
      classic_protocol::status::autocommit};
  eof_packet.status_flags(eof_status_flags);
  using eof_frame =
      classic_protocol::frame::Frame<classic_protocol::message::server::Eof>;
  const auto encode_result = classic_protocol::encode<eof_frame>(
      {generate_sequence_number(), eof_packet}, get_shared_capabilities(),
      boost::asio::dynamic_buffer(result_buffers.emplace_back()));
  if (!encode_result) {
    throw boost::system::system_error{encode_result.error()};
  }
}

} // namespace minimysql
