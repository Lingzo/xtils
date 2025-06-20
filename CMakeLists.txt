cmake_minimum_required(VERSION 4.0)

project(http_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
file(GLOB EX_SRCS main.cc json_test.cc config_example.cc config_test.cc inspect_example.cc)
file(GLOB SRCS "*.cc")
list(REMOVE_ITEM SRCS ${EX_SRCS})
add_executable(http_server ${SRCS} main.cc)
add_link_options(-rdynamic)
target_link_options(http_server PUBLIC -rdynamic)

add_executable(json_test ${SRCS} json_test.cc)

add_executable(config_test ${SRCS} config_test.cc)
add_executable(inspect_example ${SRCS} inspect_example.cc)
