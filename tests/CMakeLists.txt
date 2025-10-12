cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(add_xtils_test TARGET_NAME)
    set(${TARGET_NAME}_SRCS ${ARGN})
    add_executable(${TARGET_NAME} ${${TARGET_NAME}_SRCS})
    target_link_libraries(${TARGET_NAME} PRIVATE xtils)

    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    set_property(TEST ${TARGET_NAME}
        PROPERTY ENVIRONMENT "DOCTEST_CONFIG_COLORS_ANSI=1")
endfunction()

add_xtils_test(test_fsm fsm_test.cc)
add_xtils_test(test_json json_test.cc)
add_xtils_test(test_config config_test.cc)
add_xtils_test(test_file_utils file_utils_test.cc)
add_xtils_test(test_event event_test.cc)
add_xtils_test(test_byte_utils byte_utils_test.cc)
add_xtils_test(test_cron_scheduler cron_scheduler_test.cc)
