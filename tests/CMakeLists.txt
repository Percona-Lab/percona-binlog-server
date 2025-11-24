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

set(tag_source_files
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/tag_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/tag.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/tag.cpp
)

set(gtid_source_files
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/gtid_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/gtid.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/gtid.cpp
)

set(gtid_set_source_files
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/gtid_set_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/gtid_set.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/gtid_set.cpp
)

add_executable(tag_test tag_test.cpp ${tag_source_files})
target_include_directories(tag_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tag_test
  PRIVATE
    binlog_server_compiler_flags
    Boost::unit_test_framework
)
set_target_properties(tag_test PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_executable(gtid_test gtid_test.cpp ${tag_source_files} ${gtid_source_files})
target_include_directories(gtid_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gtid_test
  PRIVATE
    binlog_server_compiler_flags
    Boost::unit_test_framework
)
set_target_properties(gtid_test PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_executable(gtid_set_test gtid_set_test.cpp ${tag_source_files} ${gtid_source_files} ${gtid_set_source_files})
target_include_directories(gtid_set_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gtid_set_test
  PRIVATE
    binlog_server_compiler_flags
    Boost::unit_test_framework
)
set_target_properties(gtid_set_test PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

add_test(NAME tag_test COMMAND tag_test)
add_test(NAME gtid_test COMMAND gtid_test)
add_test(NAME gtid_set_test COMMAND gtid_set_test)
