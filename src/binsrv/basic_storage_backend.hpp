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

  void open_stream(std::string_view name);
  void write_data_to_stream(util::const_byte_span data);
  void close_stream();

private:
  bool stream_opened_{false};

  [[nodiscard]] virtual storage_object_name_container do_list_objects() = 0;
  [[nodiscard]] virtual std::string do_get_object(std::string_view name) = 0;
  virtual void do_put_object(std::string_view name,
                             util::const_byte_span content) = 0;

  virtual void do_open_stream(std::string_view name) = 0;
  virtual void do_write_data_to_stream(util::const_byte_span data) = 0;
  virtual void do_close_stream() = 0;
};

} // namespace binsrv

#endif // BINSRV_BASIC_STORAGE_BACKEND_HPP
