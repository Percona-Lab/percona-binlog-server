cmake_minimum_required(VERSION 3.20)

project(BinlogServer VERSION 0.1 LANGUAGES CXX)

# specify the C++ standard
add_library(binlog_server_compiler_flags INTERFACE)
target_compile_features(binlog_server_compiler_flags INTERFACE cxx_std_20)
target_include_directories(binlog_server_compiler_flags INTERFACE "${PROJECT_SOURCE_DIR}/src")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)
include(compiler_warnings)

# add compiler warning flags just when building this project via
# the BUILD_INTERFACE genex
set(gcc_cxx "$<COMPILE_LANG_AND_ID:CXX,GNU>")
set(clang_cxx "$<COMPILE_LANG_AND_ID:CXX,ARMClang,AppleClang,Clang>")
set(msvc_cxx "$<COMPILE_LANG_AND_ID:CXX,MSVC>")
target_compile_options(binlog_server_compiler_flags INTERFACE
  "$<${gcc_cxx}:$<BUILD_INTERFACE:${gcc_warnings}>>"
  "$<${clang_cxx}:$<BUILD_INTERFACE:${clang_warnings}>>"
  "$<${msvc_cxx}:$<BUILD_INTERFACE:${msvc_warnings}>>"
)

find_package(Boost 1.82.0 EXACT REQUIRED)

find_package(MySQL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(zstd REQUIRED)
find_package(ZLIB REQUIRED)

set(source_files
  src/app.cpp

  src/binsrv/basic_logger_fwd.hpp
  src/binsrv/basic_logger.hpp
  src/binsrv/basic_logger.cpp

  src/binsrv/exception_handling_helpers.hpp
  src/binsrv/exception_handling_helpers.cpp

  src/binsrv/event_flag_fwd.hpp
  src/binsrv/event_flag.hpp

  src/binsrv/event_header_fwd.hpp
  src/binsrv/event_header.hpp
  src/binsrv/event_header.cpp

  src/binsrv/event_type_fwd.hpp
  src/binsrv/event_type.hpp

  src/binsrv/log_severity_fwd.hpp
  src/binsrv/log_severity.hpp

  src/binsrv/ostream_logger.hpp
  src/binsrv/ostream_logger.cpp

  src/util/command_line_helpers_fwd.hpp
  src/util/command_line_helpers.hpp
  src/util/command_line_helpers.cpp

  src/util/conversion_helpers.hpp

  src/util/ct_string.hpp

  src/util/exception_helpers.hpp

  src/util/flag_set_fwd.hpp
  src/util/flag_set.hpp

  src/util/impl_helpers.hpp

  src/util/nv_tuple.hpp
  src/util/nv_tuple_from_command_line.hpp
  src/util/nv_tuple_from_json.hpp

  src/easymysql/core_error_helpers_private.hpp
  src/easymysql/core_error_helpers_private.cpp
  src/easymysql/core_error.hpp
  src/easymysql/core_error.cpp

  src/easymysql/binlog_deimpl_private.hpp
  src/easymysql/binlog_fwd.hpp
  src/easymysql/binlog.hpp
  src/easymysql/binlog.cpp

  src/easymysql/connection_deimpl_private.hpp
  src/easymysql/connection_fwd.hpp
  src/easymysql/connection.hpp
  src/easymysql/connection.cpp

  src/easymysql/connection_config_fwd.hpp
  src/easymysql/connection_config.hpp
  src/easymysql/connection_config.cpp

  src/easymysql/library_fwd.hpp
  src/easymysql/library.hpp
  src/easymysql/library.cpp
)

add_executable(binlog_server ${source_files})
target_link_libraries(binlog_server
  PUBLIC
     binlog_server_compiler_flags
  PRIVATE
    Boost::headers MySQL::client OpenSSL::Crypto OpenSSL::SSL zstd::libzstd_shared ZLIB::ZLIB
)
# for some reason it is not possible to propagate CXX_EXTENSIONS and
# CXX_STANDARD_REQUIRED via interface library (binlog_server_compiler_flags)
set_target_properties(binlog_server PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
