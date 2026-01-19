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

#ifndef BINSRV_BASIC_STORAGE_BACKEND_HPP
#define BINSRV_BASIC_STORAGE_BACKEND_HPP

#include "binsrv/basic_storage_backend_fwd.hpp" // IWYU pragma: export

#include <string>
#include <string_view>

#include "util/byte_span_fwd.hpp"

namespace binsrv {

class basic_storage_backend {
public:
  basic_storage_backend() = default;
  basic_storage_backend(const basic_storage_backend &) = delete;
  basic_storage_backend(basic_storage_backend &&) noexcept = delete;
  basic_storage_backend &operator=(const basic_storage_backend &) = delete;
  basic_storage_backend &operator=(basic_storage_backend &&) = delete;

  virtual ~basic_storage_backend() = default;

  [[nodiscard]] storage_object_name_container list_objects();
  [[nodiscard]] std::string get_object(std::string_view name);
  void put_object(std::string_view name, util::const_byte_span content);

  [[nodiscard]] bool is_stream_open() const noexcept { return stream_open_; }
  [[nodiscard]] std::uint64_t
  open_stream(std::string_view name, storage_backend_open_stream_mode mode);
  void write_data_to_stream(util::const_byte_span data);
  void close_stream();

  [[nodiscard]] std::string get_description() const;

private:
  bool stream_open_{false};

  [[nodiscard]] virtual storage_object_name_container do_list_objects() = 0;
  [[nodiscard]] virtual std::string do_get_object(std::string_view name) = 0;
  virtual void do_put_object(std::string_view name,
                             util::const_byte_span content) = 0;

  [[nodiscard]] virtual std::uint64_t
  do_open_stream(std::string_view name,
                 storage_backend_open_stream_mode mode) = 0;
  virtual void do_write_data_to_stream(util::const_byte_span data) = 0;
  virtual void do_close_stream() = 0;

  [[nodiscard]] virtual std::string do_get_description() const = 0;
};

} // namespace binsrv

#endif // BINSRV_BASIC_STORAGE_BACKEND_HPP
