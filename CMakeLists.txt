cmake_minimum_required(VERSION 3.20)

project(BinlogServer VERSION 0.1 LANGUAGES CXX)

# specify the C++ standard
add_library(binlog_server_compiler_flags INTERFACE)
target_compile_features(binlog_server_compiler_flags INTERFACE cxx_std_20)

# Default set of warnings is taken from here
# https://github.com/aminya/project_options/blob/main/src/CompilerWarnings.cmake
set(msvc_warnings
  /WX	# Treats all compiler warnings as errors
  /W4 # Baseline reasonable warnings
  /w14242 # 'identifier': conversion from 'type1' to 'type1', possible loss of data
  /w14254 # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits', possible loss of data
  /w14263 # 'function': member function does not override any base class virtual member function
  /w14265 # 'classname': class has virtual functions, but destructor is not virtual instances of this class may not
          # be destructed correctly
  /w14287 # 'operator': unsigned/negative constant mismatch
  /we4289 # nonstandard extension used: 'variable': loop control variable declared in the for-loop is used outside
          # the for-loop scope
  /w14296 # 'operator': expression is always 'boolean_value'
  /w14311 # 'variable': pointer truncation from 'type1' to 'type2'
  /w14545 # expression before comma evaluates to a function which is missing an argument list
  /w14546 # function call before comma missing argument list
  /w14547 # 'operator': operator before comma has no effect; expected operator with side-effect
  /w14549 # 'operator': operator before comma has no effect; did you intend 'operator'?
  /w14555 # expression has no effect; expected expression with side- effect
  /w14619 # pragma warning: there is no warning number 'number'
  /w14640 # Enable warning on thread un-safe static member initialization
  /w14826 # Conversion from 'type1' to 'type_2' is sign-extended. This may cause unexpected runtime behavior.
  /w14905 # wide string literal cast to 'LPSTR'
  /w14906 # string literal cast to 'LPWSTR'
  /w14928 # illegal copy-initialization; more than one user-defined conversion has been implicitly applied
  /permissive- # standards conformance mode for MSVC compiler.
)

set(clang_warnings
  -Werror
  -Wall
  -Wextra # reasonable and standard
  -Wextra-semi # Warn about semicolon after in-class function definition.
  -Wshadow # warn the user if a variable declaration shadows one from a parent context
  -Wnon-virtual-dtor # warn the user if a class with virtual functions has a non-virtual destructor. This helps
  # catch hard to track down memory errors
  -Wold-style-cast # warn for c-style casts
  -Wcast-align # warn for potential performance problem casts
  -Wunused # warn on anything being unused
  -Woverloaded-virtual # warn if you overload (not override) a virtual function
  -Wpedantic # warn if non-standard C++ is used
  -Wconversion # warn on type conversions that may lose data
  -Wsign-conversion # warn on sign conversions
  -Wnull-dereference # warn if a null dereference is detected
  -Wdouble-promotion # warn if float is implicit promoted to double
  -Wformat=2 # warn on security issues around functions that format output (ie printf)
  -Wimplicit-fallthrough # warn on statements that fallthrough without an explicit annotation
)

set(gcc_warnings
  ${clang_warnings}
  -Wmisleading-indentation # warn if indentation implies blocks where blocks do not exist
  -Wduplicated-cond # warn if if / else chain has duplicated conditions
  -Wduplicated-branches # warn if if / else branches have duplicated code
  -Wlogical-op # warn about logical operations being used where bitwise were probably wanted
  -Wuseless-cast # warn if you perform a cast to the same type
)

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

add_executable(binlog_server app.cpp)
target_link_libraries(binlog_server PUBLIC binlog_server_compiler_flags)
# for some reason it is not possible to propagate CXX_EXTENSIONS and
# CXX_STANDARD_REQUIRED via interface library (binlog_server_compiler_flags)
set_target_properties(binlog_server PROPERTIES
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
