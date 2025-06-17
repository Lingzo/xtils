cmake_minimum_required(VERSION 4.0)

project(http_server)

file(GLOB SRCS "*.cc")
add_executable(http_server ${SRCS})
target_link_options(http_server PUBLIC -rdynamic)
