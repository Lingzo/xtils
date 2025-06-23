cmake_minimum_required(VERSION 4.0)

project(xtils)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

file(GLOB_RECURSE SRCS src/*.cc)
# Create a single library
add_library(xtils STATIC ${SRCS})
target_include_directories(xtils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_subdirectory(tests)

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
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xtils
)
