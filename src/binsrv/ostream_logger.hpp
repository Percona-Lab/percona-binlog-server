#ifndef BINSRV_OSTREAM_LOGGER_HPP
#define BINSRV_OSTREAM_LOGGER_HPP

#include <ostream>

#include "binsrv/basic_logger.hpp"

namespace binsrv {

class [[nodiscard]] ostream_logger : public basic_logger {
public:
  ostream_logger(log_severity min_level, std::ostream &stream)
      : basic_logger{min_level}, stream_{&stream} {}

private:
  std::ostream *stream_;

  void do_log(std::string_view message) override;
};

} // namespace binsrv

#endif // BINSRV_OSTREAM_LOGGER_HPP
