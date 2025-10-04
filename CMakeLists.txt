cmake_minimum_required(VERSION 4.0)

project(xtils)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(INSPECT_DISABLE "Disable inspect module" OFF)
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_EXAMPLES "Build examples" ON)

include(cmake/xtils-autogen.cmake)

file(GLOB_RECURSE SRCS src/*.cc)
# Create a single library
add_library(xtils STATIC ${SRCS})
target_include_directories(xtils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(INSPECT_DISABLE)
    target_compile_definitions(xtils PUBLIC -DINSPECT_DISABLE=1)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif()
target_compile_options(xtils PUBLIC
    $<$<CONFIG:Debug>:-g -O0 -DENABLE_TRACE_LOGGING -DENABLE_TRACE_RECORDING -fno-omit-frame-pointer>
    $<$<CONFIG:Release>:-O2 -DNDEBUG>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
    $<$<CONFIG:MinSizeRel>:-Os -DNDEBUG>)
target_link_options(xtils PUBLIC
    $<$<CONFIG:Debug>:-rdynamic -Wl,--as-needed -Wl,--gc-sections>
    $<$<CONFIG:Release>:-rdynamic -Wl,--as-needed -Wl,--gc-sections>
    $<$<CONFIG:RelWithDebInfo>:-rdynamic -Wl,--as-needed -Wl,--gc-sections>)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation configuration
include(GNUInstallDirs)

# Install the library
install(TARGETS xtils
    EXPORT xtils-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install headers
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Export targets
install(EXPORT xtils-targets
    FILE xtils-targets.cmake
    NAMESPACE xtils::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xtils
)

# Create config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    xtils-config-version.cmake
    VERSION 1.0.0
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/xtils-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/xtils-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xtils
)

# Install config files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/xtils-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/xtils-config-version.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/xtils-autogen.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xtils
)
