#ifndef BINSRV_OSTREAM_LOGGER_HPP
#define BINSRV_OSTREAM_LOGGER_HPP

#include <string_view>

#include "binsrv/basic_logger.hpp"

namespace binsrv {

class [[nodiscard]] cout_logger : public basic_logger {
public:
  explicit cout_logger(log_severity min_level) : basic_logger{min_level} {}
  cout_logger(const cout_logger &) = delete;
  cout_logger &operator=(const cout_logger &) = delete;
  cout_logger(cout_logger &&) = delete;
  cout_logger &operator=(cout_logger &&) = delete;

  ~cout_logger() noexcept override;

private:
  void do_log(std::string_view message) override;
};

} // namespace binsrv

#endif // BINSRV_OSTREAM_LOGGER_HPP
