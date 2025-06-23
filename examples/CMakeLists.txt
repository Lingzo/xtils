cmake_minimum_required(VERSION 3.10)

# Find required packages
find_package(Threads REQUIRED)

# Function to create example executables
function(add_example name source)
    add_executable(${name} ${source})
    target_link_libraries(${name}
        xtils
        Threads::Threads
    )
    target_compile_options(${name} PRIVATE
        -Wall -Wextra -Wpedantic
    )

    # Set output directory
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
    )
endfunction()

# Add all example executables
add_example(basic_app basic_app.cc)
add_example(fsm_example fsm_example.cc)
add_example(inspect_example inspect_example.cc)
add_example(logger_example logger_example.cc)

# Optional: Create a custom target to build all examples
add_custom_target(examples
    DEPENDS
        basic_app
        fsm_example
        inspect_example
        logger_example
)

# Installation (optional)
install(TARGETS
    basic_app
    fsm_example
    inspect_example
    logger_example
    DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
    OPTIONAL
)
