#include <cstdint>
#include <iostream>

#include <boost/json/src.hpp>

int main(int /*argc*/, char * /*argv*/[]) {
  boost::json::object obj;
  obj["host"] = "127.0.0.1";
  obj["port"] = static_cast<std::uint16_t>(3306);
  obj["user"] = "root";
  obj["password"] = "password";

  std::cout << "c++ standard version: " << __cplusplus << '\n';
  std::cout << "serialized configuration: " << boost::json::serialize(obj)
            << '\n';
  return 0;
}
