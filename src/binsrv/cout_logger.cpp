#include "binsrv/cout_logger.hpp"

#include <iostream>
#include <ostream>
#include <string_view>

namespace binsrv {

void cout_logger::do_log(std::string_view message) {
  std::cout << message << '\n';
  std::cout.flush();
}

} // namespace binsrv
