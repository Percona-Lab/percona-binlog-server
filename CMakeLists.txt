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

find_package(Boost 1.81.0 EXACT REQUIRED)

find_package(MySQL REQUIRED)

set(source_files
  src/app.cpp
  src/binsrv/connection_config.hpp
  src/binsrv/connection_config.cpp
)

add_executable(binlog_server ${source_files})
target_link_libraries(binlog_server PUBLIC binlog_server_compiler_flags PRIVATE Boost::headers MySQL::client)
# for some reason it is not possible to propagate CXX_EXTENSIONS and
# CXX_STANDARD_REQUIRED via interface library (binlog_server_compiler_flags)
set_target_properties(binlog_server PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
