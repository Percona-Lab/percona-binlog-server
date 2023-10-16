#ifndef BINSRV_FILE_LOGGER_HPP
#define BINSRV_FILE_LOGGER_HPP

#include <fstream>
#include <string>
#include <string_view>

#include "binsrv/basic_logger.hpp" // IWYU pragma: export
#include "binsrv/log_severity_fwd.hpp"

namespace binsrv {

class [[nodiscard]] file_logger : public basic_logger {
public:
  file_logger(log_severity min_level, std::string_view file_name);

private:
  std::ofstream stream_;

  void do_log(std::string_view message) override;
};

} // namespace binsrv

#endif // BINSRV_FILE_LOGGER_HPP
