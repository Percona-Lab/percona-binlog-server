#include "binsrv/basic_storage_backend.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

#include "util/byte_span_fwd.hpp"
#include "util/exception_location_helpers.hpp"

namespace binsrv {

[[nodiscard]] storage_object_name_container
basic_storage_backend::list_objects() {
  return do_list_objects();
}

[[nodiscard]] std::string
basic_storage_backend::get_object(std::string_view name) {
  return do_get_object(name);
}

void basic_storage_backend::put_object(std::string_view name,
                                       util::const_byte_span content) {
  return do_put_object(name, content);
}

void basic_storage_backend::open_stream(std::string_view name) {
  if (stream_opened_) {
    util::exception_location().raise<std::logic_error>(
        "cannot open a new stream as the previous one has not been closed");
  }

  do_open_stream(name);
  stream_opened_ = true;
}

void basic_storage_backend::write_data_to_stream(util::const_byte_span data) {
  if (!stream_opened_) {
    util::exception_location().raise<std::logic_error>(
        "cannot write to the stream as it has not been opened");
  }
  do_write_data_to_stream(data);
}

void basic_storage_backend::close_stream() {
  if (!stream_opened_) {
    util::exception_location().raise<std::logic_error>(
        "cannot close the stream as it has not been opened");
  }
  do_close_stream();
  stream_opened_ = false;
}

} // namespace binsrv
