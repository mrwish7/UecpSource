cmake_minimum_required(VERSION 3.20)
project(UecpSource LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# OFF builds only the headless uecp_source_cli, skipping GLFW/ImGui/OpenGL
# entirely -- so a display-less Linux box needs no X11/GL dev packages.
option(UECP_BUILD_GUI "Build the uecp_source GUI (requires GLFW/OpenGL)" ON)

# Statically link the MSVC runtime so uecp_source.exe runs on any Windows x64
# machine with no VC++ Redistributable installed. Must be set before any
# target (including fetched/vendored GLFW/ImGui/fdk-aac/toolame) is created.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Deliberately NOT a global add_compile_options() here (unlike MPXpack): the
# strict -Wall -Wextra/-W4 bar this project holds its OWN code to (see
# src/common/CMakeLists.txt and src/app/CMakeLists.txt) is set per-target
# instead, so it doesn't leak into the vendored libtoolame-dab or the fetched
# fdk-aac -- old C encoder code neither of us wrote or intends to "clean up"
# would otherwise drown real warnings in third-party-code noise.

# --- libtoolame-dab: vendored (locally modified -- see its
#     MODIFICATIONS.md), built via our own thin CMakeLists.txt. ---
add_subdirectory(libtoolame-dab)

# --- fdk-aac, GLFW, Dear ImGui, miniaudio: fetched at configure time. ---
include(cmake/FetchDeps.cmake)

add_subdirectory(src/common)
add_subdirectory(src/app)
