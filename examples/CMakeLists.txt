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
add_example(tcp_example tcp_example.cc)
add_example(timer_example timer_example.cc)
add_example(udp_example udp_example.cc)
add_example(udp_multicast_example udp_multicast_example.cc)
add_example(cron_scheduler_example cron_scheduler_example.cc)
add_example(websocket_client_example websocket_client_example.cc)

# Optional: Create a custom target to build all examples
add_custom_target(examples
    DEPENDS
        basic_app
        fsm_example
        inspect_example
        logger_example
        tcp_example
        timer_example
        udp_example
        udp_multicast_example
        cron_scheduler_example
        websocket_client_example
)

# Installation (optional)
install(TARGETS
    basic_app
    fsm_example
    inspect_example
    logger_example
    tcp_example
    timer_example
    udp_example
    udp_multicast_example
    cron_scheduler_example
    websocket_client_example
    DESTINATION share/${PROJECT_NAME}/examples
    OPTIONAL
)
