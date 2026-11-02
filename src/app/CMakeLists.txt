# Pipeline + persisted settings, shared by the GUI and the headless CLI.
add_library(uecp_source_core STATIC
    source_app.h
    source_app.cpp
    source_settings.h
    source_settings.cpp
)
target_include_directories(uecp_source_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uecp_source_core PUBLIC ucs_common)

set(UECP_TARGETS uecp_source_core uecp_source_cli)

if(UECP_BUILD_GUI)
    add_executable(uecp_source
        main.cpp
        source_gui.h
        source_gui.cpp
    )
    target_link_libraries(uecp_source PRIVATE uecp_source_core imgui)
    if(WIN32)
        set_target_properties(uecp_source PROPERTIES WIN32_EXECUTABLE FALSE) # keep the console for log output during development
    endif()
    list(APPEND UECP_TARGETS uecp_source)
endif()

# No GLFW/ImGui/OpenGL dependency, so it runs on machines without a display.
add_executable(uecp_source_cli
    cli_main.cpp
)
target_link_libraries(uecp_source_cli PRIVATE uecp_source_core)

foreach(target ${UECP_TARGETS})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /wd4996)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()
