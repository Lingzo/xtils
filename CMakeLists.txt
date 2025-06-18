cmake_minimum_required(VERSION 4.0)

project(http_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
file(GLOB EX_SRCS main.cc json_test.cc)
file(GLOB SRCS "*.cc")
list(REMOVE_ITEM SRCS ${EX_SRCS})
add_executable(http_server ${SRCS} main.cc)
target_link_options(http_server PUBLIC -rdynamic)

add_executable(json_test ${SRCS} json_test.cc)