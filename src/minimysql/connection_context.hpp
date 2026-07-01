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

#ifndef MINIMYSQL_CONNECTION_CONTEXT_HPP
#define MINIMYSQL_CONNECTION_CONTEXT_HPP

#include "minimysql/connection_context_fwd.hpp" // IWYU pragma: export

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "minimysql/network_io_operations_fwd.hpp"

namespace minimysql {

class connection_context {
public:
  struct datatype_properties_type;

  // Constants for the greetings packet
  static constexpr std::uint8_t default_server_protocol_version{10U};
  static constexpr std::string_view default_server_version{"9.7.0-pbs"};
  static constexpr std::uint16_t default_server_status_flags{0U};
  static constexpr std::uint8_t default_server_collation{0U};
  static constexpr std::string_view default_server_auth_method{
      "caching_sha2_password"};

  connection_context(std::string_view server_username,
                     std::string_view server_password);

  [[nodiscard]] const std::string &get_server_username() const noexcept {
    return server_username_;
  }
  [[nodiscard]] const std::string &get_server_password() const noexcept {
    return server_password_;
  }
  [[nodiscard]] bool check_client_authentication() const;

  [[nodiscard]] std::uint32_t get_connection_id() const noexcept {
    return connection_id_;
  }
  [[nodiscard]] std::uint8_t get_sequence_number() const noexcept {
    return sequence_number_;
  }

  [[nodiscard]] const capability_bitset &
  get_server_capabilities() const noexcept {
    return server_capabilities_;
  }
  [[nodiscard]] const capability_bitset &
  get_client_capabilities() const noexcept {
    return client_capabilities_;
  }
  [[nodiscard]] capability_bitset get_shared_capabilities() const noexcept {
    return client_capabilities_ & server_capabilities_;
  }
  [[nodiscard]] bool check_shared_plugin_auth_supported() const;
  [[nodiscard]] bool
  check_shared_text_result_with_session_tracking_supported() const;

  [[nodiscard]] const std::string &get_server_auth_method() const noexcept {
    return server_auth_method_;
  }
  [[nodiscard]] const std::string &
  get_server_auth_method_data() const noexcept {
    return server_auth_method_data_;
  }

  [[nodiscard]] const std::string &get_client_auth_method() const noexcept {
    return client_auth_method_;
  }
  [[nodiscard]] const std::string &
  get_client_auth_method_data() const noexcept {
    return client_auth_method_data_;
  }

  [[nodiscard]] const std::string &get_client_username() const noexcept {
    return client_username_;
  }
  [[nodiscard]] const std::string &get_client_schema() const noexcept {
    return client_schema_;
  }
  [[nodiscard]] std::uint8_t get_client_collation() const noexcept {
    return client_collation_;
  }
  [[nodiscard]] std::uint32_t get_client_max_packet_size() const noexcept {
    return client_max_packet_size_;
  }
  [[nodiscard]] const std::string &get_client_attributes() const noexcept {
    return client_attributes_;
  }

  [[nodiscard]] client_command_type get_client_mysql_command() const noexcept {
    return client_command_;
  }
  [[nodiscard]] const std::string &get_client_statement() const noexcept {
    return client_statement_;
  }

  [[nodiscard]] std::uint16_t get_binlog_flags() const noexcept {
    return binlog_flags_;
  }
  [[nodiscard]] bool check_binlog_non_blocking_dump() const noexcept;
  [[nodiscard]] std::uint32_t get_binlog_server_id() const noexcept {
    return binlog_server_id_;
  }
  [[nodiscard]] const std::string &get_binlog_filename() const noexcept {
    return binlog_filename_;
  }
  [[nodiscard]] std::uint64_t get_binlog_position() const noexcept {
    return binlog_position_;
  }

  [[nodiscard]] network_buffer_type generate_encoded_server_greeting();
  void parse_client_greeting(const network_buffer_type &payload);

  [[nodiscard]] network_buffer_type generate_encoded_fast_auth();
  [[nodiscard]] network_buffer_type generate_encoded_ok();
  [[nodiscard]] network_buffer_type generate_encoded_eof();
  [[nodiscard]] network_buffer_type
  generate_encoded_error(std::uint16_t error_code, std::string_view message,
                         std::string_view sql_state);
  [[nodiscard]] network_buffer_type generate_encoded_access_denied();
  [[nodiscard]] network_buffer_type generate_encoded_unknown_command();
  [[nodiscard]] network_buffer_type generate_encoded_syntax_error();

  [[nodiscard]] network_buffer_type
  generate_encoded_binlog_event(std::string_view event_data);

  void enter_command_loop_iteration() noexcept {
    sequence_number_ = 0U;
    client_command_ = client_command_type::unknown;
    client_statement_.clear();
  }
  void parse_client_command(const network_buffer_type &payload);

  template <typename... TypePack>
  [[nodiscard]] network_buffer_container encode_resultset(
      const std::vector<std::tuple<TypePack...>> &rows,
      std::span<const column_name_pair, sizeof...(TypePack)> column_names) {
    static constexpr std::size_t number_of_columns{
        std::tuple_size_v<std::tuple<TypePack...>>};
    const std::size_t number_of_rows{std::size(rows)};

    const bool text_result_with_session_tracking_supported{
        check_shared_text_result_with_session_tracking_supported()};
    network_buffer_container result_buffers{};
    // 1 for column count, 1 per column + (0 or 1) for intermediate EOF, 1 per
    // row + 1 for EOF
    result_buffers.reserve(
        1UZ + number_of_columns +
        (text_result_with_session_tracking_supported ? 1UZ : 0UZ) +
        number_of_rows + 1UZ);

    // part 1: column count
    encode_resultset_number_of_columns_internal(result_buffers,
                                                number_of_columns);

    // part 2: column definition(s)
    encode_resultset_column_definitions_internal<TypePack...>(
        result_buffers, "db", "tbl", column_names);

    // part 2a: intermediate eof packet that is needed only when text resultset
    // with session tracking is NOT supported by the client.
    if (!text_result_with_session_tracking_supported) {
      encode_resultset_intermediate_eof_internal(result_buffers);
    }

    // part 3: row data
    for (const auto &row : rows) {
      encode_resultset_row_internal(result_buffers, row);
    }

    // part 4: EOF
    encode_resultset_eof_internal(result_buffers);

    return result_buffers;
  }

private:
  static std::atomic_uint32_t next_connection_id_;

  std::string server_username_;
  std::string server_password_;

  std::uint32_t connection_id_;
  std::uint8_t sequence_number_{0U};

  capability_bitset server_capabilities_{};
  capability_bitset client_capabilities_{};

  std::string server_auth_method_{};
  std::string server_auth_method_data_{};

  std::string client_auth_method_{};
  std::string client_auth_method_data_{};

  std::string client_username_{};
  std::string client_schema_{};
  std::uint8_t client_collation_{0U};
  std::uint32_t client_max_packet_size_{0U};
  std::string client_attributes_{};

  client_command_type client_command_{client_command_type::unknown};
  std::string client_statement_{};

  std::uint16_t binlog_flags_{};
  std::uint32_t binlog_server_id_{};
  std::string binlog_filename_{};
  std::uint64_t binlog_position_{};

  [[nodiscard]] static capability_bitset
  get_default_server_capabilities() noexcept;
  [[nodiscard]] const std::string &generate_server_auth_method_data();
  [[nodiscard]] std::uint8_t generate_sequence_number();
  void validate_and_update_sequence_number(std::uint8_t sequence_number);

  void encode_resultset_number_of_columns_internal(
      network_buffer_container &result_buffers, std::size_t number_of_columns);

  template <typename Type>
  static const datatype_properties_type &get_datatype_properties() noexcept;

  void encode_resultset_column_definition_internal(
      network_buffer_container &result_buffers, std::string_view db_name,
      std::string_view table_name, const column_name_pair &column_name,
      const datatype_properties_type &datatype_properties);
  template <typename... TypePack>
  void encode_resultset_column_definitions_internal(
      network_buffer_container &result_buffers,
      [[maybe_unused]] std::string_view db_name,
      [[maybe_unused]] std::string_view table_name,
      [[maybe_unused]] std::span<const column_name_pair, sizeof...(TypePack)>
          column_names) {
    std::size_t column_index{0UZ};
    (encode_resultset_column_definition_internal(
         result_buffers, db_name, table_name, column_names[column_index++],
         get_datatype_properties<TypePack>()),
     ...);
  }
  void encode_resultset_intermediate_eof_internal(
      network_buffer_container &result_buffers);

  using optional_string = std::optional<std::string>;
  using optional_string_collection = std::vector<optional_string>;
  void encode_resultset_textualized_row_internal(
      network_buffer_container &result_buffers,
      const optional_string_collection &row_values);

  template <typename Type>
  static optional_string textualize_value(const Type &value) {
    if constexpr (std::is_integral_v<Type>) {
      return std::to_string(value);
    } else if constexpr (std::is_same_v<Type, std::string>) {
      return value;
    }
  }
  template <typename Type>
  static optional_string textualize_value(const std::optional<Type> &value) {
    if (!value.has_value()) {
      return {};
    }
    return textualize_value(*value);
  }

  template <typename... TypePack>
  void encode_resultset_row_internal(network_buffer_container &result_buffers,
                                     const std::tuple<TypePack...> &row) {
    optional_string_collection textualized_row_values{};
    textualized_row_values.reserve(sizeof...(TypePack));
    const auto tuple_traversal_lambda{[&textualized_row_values,
                                       &row]<std::size_t... I>(
                                          std::index_sequence<I...>) {
      (textualized_row_values.emplace_back(textualize_value(std::get<I>(row))),
       ...);
    }};
    tuple_traversal_lambda(std::index_sequence_for<TypePack...>{});
    encode_resultset_textualized_row_internal(result_buffers,
                                              textualized_row_values);
  }
  void encode_resultset_eof_internal(network_buffer_container &result_buffers);
};

} // namespace minimysql

#endif // MINIMYSQL_CONNECTION_CONTEXT_HPP
