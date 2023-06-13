#include "binsrv/file_logger.hpp"

#include <filesystem>

#include "util/exception_location_helpers.hpp"

namespace binsrv {

file_logger::file_logger(log_severity min_level, std::string_view file_name)
    : basic_logger{min_level}, stream_{std::filesystem::path{file_name}} {
  if (!stream_.is_open()) {
    util::exception_location().raise<std::invalid_argument>(
        "unable to create \"" + std::string(file_name) + "\" file for logging");
  }
}

void file_logger::do_log(std::string_view message) {
  stream_ << message << std::endl;
}

} // namespace binsrv
