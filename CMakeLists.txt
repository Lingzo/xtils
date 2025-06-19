cmake_minimum_required(VERSION 4.0)

project(http_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
file(GLOB EX_SRCS main.cc json_test.cc config_example.cc config_test.cc cmdline_test.cc hierarchy_test.cc complete_example.cc config_file_example.cc config_native_types_test.cc array_define_test.cc simple_array_example.cc)
file(GLOB SRCS "*.cc")
list(REMOVE_ITEM SRCS ${EX_SRCS})
add_executable(http_server ${SRCS} main.cc)
target_link_options(http_server PUBLIC -rdynamic)

add_executable(json_test ${SRCS} json_test.cc)

add_executable(config_test ${SRCS} config_test.cc)
