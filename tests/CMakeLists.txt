cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(add_xtils_test TARGET_NAME)
    set(${TARGET_NAME}_SRCS ${ARGN})
    add_executable(${TARGET_NAME} ${${TARGET_NAME}_SRCS})
    target_link_libraries(${TARGET_NAME} PRIVATE xtils xtils-autogen)

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
add_xtils_test(test_task_group task_group_test.cc)
add_xtils_test(test_string_utils string_utils_test.cc)
add_xtils_test(test_base64 base64_test.cc)
add_xtils_test(test_sha1 sha1_test.cc)
add_xtils_test(test_time_utils time_utils_test.cc)
add_xtils_test(test_timer timer_test.cc)
add_xtils_test(test_behavior_tree behavior_tree_test.cc)
add_xtils_test(test_bt_logger bt_logger_test.cc)
add_xtils_test(test_sink sink_test.cc)
add_xtils_test(test_paged_memory paged_memory_test.cc)
add_xtils_test(test_unix_socket unix_socket_test.cc)
add_xtils_test(test_tcp tcp_test.cc)
add_xtils_test(test_udp udp_test.cc)
add_xtils_test(test_http http_test.cc)
add_xtils_test(test_signal_handler signal_handler_test.cc)

# Network tests use real sockets/ports - serialize to avoid resource conflicts
set_tests_properties(test_unix_socket test_tcp test_udp test_http
    PROPERTIES RESOURCE_LOCK "network_port")
add_xtils_test(test_http_common http_common_test.cc)
