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

find_package(Boost REQUIRED COMPONENTS unit_test_framework)

add_executable(byte_span_encoding_test byte_span_encoding_test.cpp)
target_include_directories(byte_span_encoding_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(byte_span_encoding_test
  PRIVATE
    binlog_server_compiler_flags
    binsrv::lib_util
    Boost::unit_test_framework
)
set_target_properties(byte_span_encoding_test PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_executable(uuid_test uuid_test.cpp)
target_include_directories(uuid_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(uuid_test
  PRIVATE
    binlog_server_compiler_flags
    binsrv::lib_gtids
    Boost::unit_test_framework
)
set_target_properties(uuid_test PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_executable(tag_test tag_test.cpp)
target_include_directories(tag_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tag_test
  PRIVATE
    binlog_server_compiler_flags
    binsrv::lib_gtids
    Boost::unit_test_framework
)
set_target_properties(tag_test PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_executable(gtid_test gtid_test.cpp)
target_include_directories(gtid_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gtid_test
  PRIVATE
    binlog_server_compiler_flags
    binsrv::lib_gtids
    Boost::unit_test_framework
)
set_target_properties(gtid_test PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_executable(gtid_set_test gtid_set_test.cpp)
target_include_directories(gtid_set_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gtid_set_test
  PRIVATE
    binlog_server_compiler_flags
    binsrv::lib_gtids
    Boost::unit_test_framework
)
set_target_properties(gtid_set_test PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_executable(event_test event_test.cpp)
target_include_directories(event_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(event_test
  PRIVATE
    binlog_server_compiler_flags
    binsrv::lib_gtids
    binsrv::lib_events
    Boost::unit_test_framework
    ZLIB::ZLIB
)
set_target_properties(event_test PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_test(NAME byte_span_encoding_test COMMAND byte_span_encoding_test)
add_test(NAME uuid_test COMMAND uuid_test)
add_test(NAME tag_test COMMAND tag_test)
add_test(NAME gtid_test COMMAND gtid_test)
add_test(NAME gtid_set_test COMMAND gtid_set_test)
add_test(NAME event_test COMMAND event_test)
