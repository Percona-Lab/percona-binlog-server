#ifndef BINSRV_OSTREAM_LOGGER_HPP
#define BINSRV_OSTREAM_LOGGER_HPP

#include <ostream>

#include "binsrv/basic_logger.hpp"

namespace binsrv {

class ostream_logger : public basic_logger {
public:
  explicit ostream_logger(std::ostream &stream)
      : basic_logger{}, stream_{&stream} {}

private:
  std::ostream *stream_;

  void do_log(std::string_view message) override;
};

} // namespace binsrv

#endif // BINSRV_OSTREAM_LOGGER_HPP
