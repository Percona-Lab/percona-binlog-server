#include "binsrv/cout_logger.hpp"

#include <iostream>

namespace binsrv {

void cout_logger::do_log(std::string_view message) {
  // flushing here deliberately
  std::cout << message << std::endl;
}

cout_logger::~cout_logger() noexcept {
  try {
    std::cout.flush();
  } catch (...) {
  }
}

} // namespace binsrv
