add_library(ucs_common STATIC
    app_config.h
    net_common.h
    net_common.cpp
    tcp_socket.h
    tcp_socket.cpp
    base64.h
    base64.cpp
    byte_ring_buffer.h
    byte_ring_buffer.cpp
    block_queue.h
    config_store.h
    config_store.cpp
    app_paths.h
    app_paths.cpp
    audio_device.h
    audio_device.cpp
    icecast_source_client.h
    icecast_source_client.cpp
    uecp_server.h
    uecp_server.cpp
    mp2_encoder.h
    mp2_encoder.cpp
    aac_encoder.h
    aac_encoder.cpp
)

target_include_directories(ucs_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ucs_common PUBLIC toolame fdk-aac miniaudio)

if(WIN32)
    target_link_libraries(ucs_common PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_options(ucs_common PRIVATE /W4 /wd4996)
else()
    target_compile_options(ucs_common PRIVATE -Wall -Wextra)
endif()
