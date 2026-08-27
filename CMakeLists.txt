cmake_minimum_required(VERSION 3.21)

project(SecureTunnelCpp
    VERSION 0.1.0
    DESCRIPTION "安全加密隧道的 C++20 工程骨架"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # 所有目标都按 UTF-8 解析源码，否则中文注释在 GBK 代码页下可能破坏语法。
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(TUNNEL_CORE_SOURCES
    src/config.cpp
    src/console_utf8.cpp
    src/development_adapters.cpp
    src/inner_session.cpp
    src/packet_buffer_pool.cpp
    src/relay_stats.cpp
    src/session.cpp
    src/tunnel_engine.cpp
)

set(TUNNEL_ENABLE_MSQUIC OFF)
if(WIN32)
    set(MSQUIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/msquic/nuget/build/native")
    if(EXISTS "${MSQUIC_ROOT}/include/msquic.h")
        set(TUNNEL_ENABLE_MSQUIC ON)
    endif()
endif()

if(TUNNEL_ENABLE_MSQUIC)
    list(APPEND TUNNEL_CORE_SOURCES
        src/msquic_api.cpp
        src/quic_secure_transport.cpp
    )
endif()

if(WIN32)
    list(APPEND TUNNEL_CORE_SOURCES
        src/wintun_loader.cpp
        src/wintun_virtual_interface.cpp
        src/windows_net_config.cpp
        src/smart_route_manager.cpp
    )
else()
    list(APPEND TUNNEL_CORE_SOURCES src/linux_tun_virtual_interface.cpp)
endif()

add_library(tunnel_core ${TUNNEL_CORE_SOURCES})

target_include_directories(tunnel_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    # 官方 wintun.h 位于 third_party/wintun。
    target_include_directories(tunnel_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wintun
    )
    # wintun.h 依赖 Winsock / 接口定义头。
    target_link_libraries(tunnel_core PUBLIC ws2_32 iphlpapi)
endif()

if(TUNNEL_ENABLE_MSQUIC)
    target_compile_definitions(tunnel_core PUBLIC TUNNEL_HAS_MSQUIC=1)
    target_include_directories(tunnel_core
        PUBLIC
            ${MSQUIC_ROOT}/include
    )
    target_link_libraries(tunnel_core
        PUBLIC
            ${MSQUIC_ROOT}/lib/x64/msquic.lib
            secur32
            bcrypt
    )
endif()

function(tunnel_copy_msquic_dll target_name)
    if(TUNNEL_ENABLE_MSQUIC)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MSQUIC_ROOT}/bin/x64/msquic.dll"
                "$<TARGET_FILE_DIR:${target_name}>/msquic.dll"
            COMMENT "复制 msquic.dll 到输出目录"
        )
    endif()
endfunction()

add_executable(tunnel_client apps/client_main.cpp)
target_link_libraries(tunnel_client PRIVATE tunnel_core)
tunnel_copy_msquic_dll(tunnel_client)

add_executable(tunnel_server apps/server_main.cpp)
target_link_libraries(tunnel_server PRIVATE tunnel_core)
tunnel_copy_msquic_dll(tunnel_server)

if(WIN32)
    add_executable(wintun_probe apps/wintun_probe.cpp)
    target_link_libraries(wintun_probe PRIVATE tunnel_core)

    # 把官方 DLL 复制到可执行文件旁，便于动态加载。
    add_custom_command(TARGET wintun_probe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wintun/wintun.dll"
            "$<TARGET_FILE_DIR:wintun_probe>/wintun.dll"
        COMMENT "复制 wintun.dll 到输出目录"
    )
    add_custom_command(TARGET tunnel_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wintun/wintun.dll"
            "$<TARGET_FILE_DIR:tunnel_client>/wintun.dll"
    )
    add_custom_command(TARGET tunnel_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/config"
            "$<TARGET_FILE_DIR:tunnel_client>/config"
        COMMENT "Copy smart-route lists to output dir"
    )
endif()

enable_testing()
add_executable(tunnel_tests tests/session_tests.cpp)
target_link_libraries(tunnel_tests PRIVATE tunnel_core)
add_test(NAME tunnel_tests COMMAND tunnel_tests)

add_executable(inner_session_tests tests/inner_session_tests.cpp)
target_link_libraries(inner_session_tests PRIVATE tunnel_core)
add_test(NAME inner_session_tests COMMAND inner_session_tests)

add_executable(data_plane_relay_tests tests/data_plane_relay_tests.cpp)
target_link_libraries(data_plane_relay_tests PRIVATE tunnel_core)
add_test(NAME data_plane_relay_tests COMMAND data_plane_relay_tests)

add_executable(packet_buffer_pool_tests tests/packet_buffer_pool_tests.cpp)
target_link_libraries(packet_buffer_pool_tests PRIVATE tunnel_core)
add_test(NAME packet_buffer_pool_tests COMMAND packet_buffer_pool_tests)

add_executable(inner_session_fuzz_tests tests/inner_session_fuzz_tests.cpp)
target_link_libraries(inner_session_fuzz_tests PRIVATE tunnel_core)
add_test(NAME inner_session_fuzz_tests COMMAND inner_session_fuzz_tests)

add_executable(relay_benchmark_tests tests/relay_benchmark_tests.cpp)
target_link_libraries(relay_benchmark_tests PRIVATE tunnel_core)
add_test(NAME relay_benchmark_tests COMMAND relay_benchmark_tests)

if(WIN32)
    add_executable(wintun_loader_tests tests/wintun_loader_tests.cpp)
    target_link_libraries(wintun_loader_tests PRIVATE tunnel_core)
    add_custom_command(TARGET wintun_loader_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wintun/wintun.dll"
            "$<TARGET_FILE_DIR:wintun_loader_tests>/wintun.dll"
    )
    add_test(NAME wintun_loader_tests COMMAND wintun_loader_tests)

    add_executable(windows_net_config_tests tests/windows_net_config_tests.cpp)
    target_link_libraries(windows_net_config_tests PRIVATE tunnel_core)
    add_test(NAME windows_net_config_tests COMMAND windows_net_config_tests)

    add_executable(smart_route_list_tests tests/smart_route_list_tests.cpp)
    target_link_libraries(smart_route_list_tests PRIVATE tunnel_core)
    add_test(NAME smart_route_list_tests COMMAND smart_route_list_tests)
    set_tests_properties(smart_route_list_tests PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

    if(TUNNEL_ENABLE_MSQUIC)
        add_executable(quic_handshake_tests tests/quic_handshake_tests.cpp)
        target_link_libraries(quic_handshake_tests PRIVATE tunnel_core)
        tunnel_copy_msquic_dll(quic_handshake_tests)
        add_test(NAME quic_handshake_tests COMMAND quic_handshake_tests)
        set_tests_properties(quic_handshake_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
    endif()
endif()
