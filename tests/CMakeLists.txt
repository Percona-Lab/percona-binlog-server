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

set(byte_span_encoding_source_files
  ${PROJECT_SOURCE_DIR}/src/util/byte_span_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/util/byte_span.hpp
  ${PROJECT_SOURCE_DIR}/src/util/byte_span_extractors.hpp
  ${PROJECT_SOURCE_DIR}/src/util/byte_span_inserters.hpp
  ${PROJECT_SOURCE_DIR}/src/util/byte_span_packed_int_constants.hpp
)

set(uuid_source_files
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/uuid_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/uuid.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/gtids/uuid.cpp
)

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

set(event_set_source_files
  ${PROJECT_SOURCE_DIR}/src/binsrv/ctime_timestamp.cpp


  ${PROJECT_SOURCE_DIR}/src/binsrv/events/anonymous_gtid_log_body_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/anonymous_gtid_log_body_impl.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/anonymous_gtid_log_post_header_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/anonymous_gtid_log_post_header_impl.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/checksum_algorithm_type_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/checksum_algorithm_type.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/code_type_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/code_type.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/common_header_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/common_header.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/common_header.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/common_header_view_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/common_header_view.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/common_header_view.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/common_header_flag_type_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/common_header_flag_type.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/empty_body_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/empty_body.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/empty_body.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/empty_post_header_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/empty_post_header.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/empty_post_header.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/event_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/event.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/event.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/event_view_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/event_view.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/event_view.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/footer_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/footer.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/footer.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/footer_view_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/footer_view.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/footer_view.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/format_description_body_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/format_description_body_impl.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/format_description_body_impl.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/format_description_post_header_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/format_description_post_header_impl.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/format_description_post_header_impl.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/generic_body_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/generic_body.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/generic_post_header_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/generic_post_header.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_flag_type_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_flag_type.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_body_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_body.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_body.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_body_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_body_impl.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_post_header_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_post_header.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_post_header.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_post_header_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_log_post_header_impl.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_tagged_log_body_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_tagged_log_body_impl.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_tagged_log_body_impl.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_tagged_log_post_header_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/gtid_tagged_log_post_header_impl.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/previous_gtids_log_body_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/previous_gtids_log_body_impl.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/previous_gtids_log_body_impl.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/previous_gtids_log_post_header_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/previous_gtids_log_post_header_impl.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/protocol_traits_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/protocol_traits.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/protocol_traits.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/reader_context_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/reader_context.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/reader_context.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/rotate_body_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/rotate_body_impl.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/rotate_body_impl.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/rotate_post_header_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/rotate_post_header_impl.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/rotate_post_header_impl.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/stop_body_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/stop_body_impl.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/stop_post_header_impl_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/stop_post_header_impl.hpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/unknown_body_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/unknown_body.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/unknown_body.cpp

  ${PROJECT_SOURCE_DIR}/src/binsrv/events/unknown_post_header_fwd.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/unknown_post_header.hpp
  ${PROJECT_SOURCE_DIR}/src/binsrv/events/unknown_post_header.cpp


  ${PROJECT_SOURCE_DIR}/src/util/semantic_version.cpp
  ${PROJECT_SOURCE_DIR}/src/util/crc_helpers.cpp
)

add_executable(byte_span_encoding_test byte_span_encoding_test.cpp ${byte_span_encoding_source_files})
target_include_directories(byte_span_encoding_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(byte_span_encoding_test
  PRIVATE
    binlog_server_compiler_flags
    Boost::unit_test_framework
)
set_target_properties(byte_span_encoding_test PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)


add_executable(uuid_test uuid_test.cpp ${uuid_source_files})
target_include_directories(uuid_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(uuid_test
  PRIVATE
    binlog_server_compiler_flags
    Boost::unit_test_framework
)
set_target_properties(uuid_test PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
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

add_executable(gtid_test gtid_test.cpp ${uuid_source_files} ${tag_source_files} ${gtid_source_files})
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

add_executable(gtid_set_test gtid_set_test.cpp ${uuid_source_files} ${tag_source_files} ${gtid_source_files} ${gtid_set_source_files})
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

add_executable(event_test event_test.cpp ${uuid_source_files} ${tag_source_files} ${gtid_source_files} ${gtid_set_source_files} ${event_set_source_files})
target_include_directories(event_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(event_test
  PRIVATE
    binlog_server_compiler_flags
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
