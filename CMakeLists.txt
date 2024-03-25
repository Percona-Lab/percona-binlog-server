# Copyright (c) 2023-2024 Percona and/or its affiliates.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

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

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(WITH_STDLIB_LIBCXX_DEFAULT ON)
  else()
    set(WITH_STDLIB_LIBCXX_DEFAULT OFF)
  endif()
  option(WITH_STDLIB_LIBCXX "Use libc++ standard library" ${WITH_STDLIB_LIBCXX_DEFAULT})

  if(WITH_STDLIB_LIBCXX)
    message(STATUS "Using libc++ standard library")
    set(libcxx_options "-stdlib=libc++")
  else()
    message(STATUS "Using libstdc++ standard library")
    set(libcxx_options "-stdlib=libstdc++")
  endif()
  target_compile_options(binlog_server_compiler_flags INTERFACE
    "$<${clang_cxx}:$<BUILD_INTERFACE:${libcxx_options}>>"
  )
  target_link_options(binlog_server_compiler_flags INTERFACE
    "$<${clang_cxx}:$<BUILD_INTERFACE:${libcxx_options}>>"
  )
endif()

option(WITH_ASAN "Enable Address Sanitizer" OFF)
if(WITH_ASAN)
  # By default during the build time, on both GCC anf Clang, LeakSanitized is
  # enabled by default when AddressSanitizer is enabled. However, in runtime
  # it is enabled by default only on Linux platforms. Therefore, on MacOS,
  # the executable needs to be run with 'ASAN_OPTIONS=detect_leaks=1'.
  # Also AddressSanitizerUseAfterScope is also enabled by default both during
  # the build time and in runtime, so there is no need to specify
  # '-fsanitize-address-use-after-scope' explicitly or modify 'ASAN_OPTIONS'
  target_compile_options(binlog_server_compiler_flags INTERFACE "-fsanitize=address")
  target_link_options(binlog_server_compiler_flags INTERFACE "-fsanitize=address")
endif()

find_package(Boost 1.83.0 EXACT REQUIRED)

find_package(MySQL REQUIRED)

find_package(ZLIB REQUIRED)
find_package(AWSSDK REQUIRED COMPONENTS s3-crt)

set(source_files
  # main application files
  src/app.cpp

  # binlog event data structure files
  src/binsrv/event/checksum_algorithm_type_fwd.hpp
  src/binsrv/event/checksum_algorithm_type.hpp

  src/binsrv/event/code_type_fwd.hpp
  src/binsrv/event/code_type.hpp

  src/binsrv/event/common_header_fwd.hpp
  src/binsrv/event/common_header.hpp
  src/binsrv/event/common_header.cpp

  src/binsrv/event/empty_body_fwd.hpp
  src/binsrv/event/empty_body.hpp
  src/binsrv/event/empty_body.cpp

  src/binsrv/event/empty_post_header_fwd.hpp
  src/binsrv/event/empty_post_header.hpp
  src/binsrv/event/empty_post_header.cpp

  src/binsrv/event/event_fwd.hpp
  src/binsrv/event/event.hpp
  src/binsrv/event/event.cpp

  src/binsrv/event/flag_type_fwd.hpp
  src/binsrv/event/flag_type.hpp

  src/binsrv/event/footer_fwd.hpp
  src/binsrv/event/footer.hpp
  src/binsrv/event/footer.cpp

  src/binsrv/event/format_description_body_impl_fwd.hpp
  src/binsrv/event/format_description_body_impl.hpp
  src/binsrv/event/format_description_body_impl.cpp

  src/binsrv/event/format_description_post_header_impl_fwd.hpp
  src/binsrv/event/format_description_post_header_impl.hpp
  src/binsrv/event/format_description_post_header_impl.cpp

  src/binsrv/event/generic_body_fwd.hpp
  src/binsrv/event/generic_body.hpp

  src/binsrv/event/generic_post_header_fwd.hpp
  src/binsrv/event/generic_post_header.hpp

  src/binsrv/event/protocol_traits_fwd.hpp
  src/binsrv/event/protocol_traits.hpp
  src/binsrv/event/protocol_traits.cpp

  src/binsrv/event/reader_context_fwd.hpp
  src/binsrv/event/reader_context.hpp
  src/binsrv/event/reader_context.cpp

  src/binsrv/event/rotate_body_impl_fwd.hpp
  src/binsrv/event/rotate_body_impl.hpp
  src/binsrv/event/rotate_body_impl.cpp

  src/binsrv/event/rotate_post_header_impl_fwd.hpp
  src/binsrv/event/rotate_post_header_impl.hpp
  src/binsrv/event/rotate_post_header_impl.cpp

  src/binsrv/event/unknown_body_fwd.hpp
  src/binsrv/event/unknown_body.hpp
  src/binsrv/event/unknown_body.cpp

  src/binsrv/event/unknown_post_header_fwd.hpp
  src/binsrv/event/unknown_post_header.hpp
  src/binsrv/event/unknown_post_header.cpp

  # binlog files
  src/binsrv/basic_logger_fwd.hpp
  src/binsrv/basic_logger.hpp
  src/binsrv/basic_logger.cpp

  src/binsrv/basic_storage_backend_fwd.hpp
  src/binsrv/basic_storage_backend.hpp
  src/binsrv/basic_storage_backend.cpp

  src/binsrv/cout_logger.hpp
  src/binsrv/cout_logger.cpp

  src/binsrv/exception_handling_helpers.hpp
  src/binsrv/exception_handling_helpers.cpp

  src/binsrv/file_logger.hpp
  src/binsrv/file_logger.cpp

  src/binsrv/filesystem_storage_backend.hpp
  src/binsrv/filesystem_storage_backend.cpp

  src/binsrv/log_severity_fwd.hpp
  src/binsrv/log_severity.hpp

  src/binsrv/logger_config_fwd.hpp
  src/binsrv/logger_config.hpp

  src/binsrv/logger_factory.hpp
  src/binsrv/logger_factory.cpp

  src/binsrv/main_config_fwd.hpp
  src/binsrv/main_config.hpp
  src/binsrv/main_config.cpp

  src/binsrv/s3_storage_backend.hpp
  src/binsrv/s3_storage_backend.cpp

  src/binsrv/storage_fwd.hpp
  src/binsrv/storage.hpp
  src/binsrv/storage.cpp

  src/binsrv/storage_backend_factory.hpp
  src/binsrv/storage_backend_factory.cpp

  src/binsrv/storage_config_fwd.hpp
  src/binsrv/storage_config.hpp

  # various utility files
  src/util/byte_span_fwd.hpp
  src/util/byte_span.hpp
  src/util/byte_span_extractors.hpp

  src/util/command_line_helpers_fwd.hpp
  src/util/command_line_helpers.hpp
  src/util/command_line_helpers.cpp

  src/util/conversion_helpers.hpp

  src/util/composite_name_fwd.hpp
  src/util/composite_name.hpp

  src/util/ct_string.hpp

  src/util/exception_location_helpers.hpp

  src/util/flag_set_fwd.hpp
  src/util/flag_set.hpp

  src/util/impl_helpers.hpp

  src/util/mixin_exception_adapter.hpp

  src/util/nv_tuple_fwd.hpp
  src/util/nv_tuple.hpp
  src/util/nv_tuple_from_command_line.hpp
  src/util/nv_tuple_from_json.hpp

  src/util/redirectable.hpp

  # mysql wrapper library files
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
    Boost::headers MySQL::client
    aws-cpp-sdk-s3-crt
)
# for some reason it is not possible to propagate CXX_EXTENSIONS and
# CXX_STANDARD_REQUIRED via interface library (binlog_server_compiler_flags)
set_target_properties(binlog_server PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
