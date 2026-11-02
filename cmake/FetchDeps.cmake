include(FetchContent)

# --- fdk-aac (AAC encoder), built via its own CMakeLists.txt ---
# mstorsjo/fdk-aac is the upstream Android external/aac source plus
# CMake/autotools build files. Unmodified, so it's fetched rather than
# vendored; override with -DFETCHCONTENT_SOURCE_DIR_FDKAAC=<path> to build
# against a local copy (e.g. offline).
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(FDK_AAC_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(FDK_AAC_INSTALL_PKGCONFIG_MODULE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    fdkaac
    GIT_REPOSITORY https://github.com/mstorsjo/fdk-aac.git
    GIT_TAG        v2.0.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(fdkaac)

if(UECP_BUILD_GUI)
# --- GLFW (windowing/OpenGL context for the Dear ImGui backend) ---
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
# X11-only on Linux: avoids requiring wayland-scanner/wayland-protocols on
# the build machine. X11 apps still run fine on Wayland desktops via
# XWayland, so this doesn't limit where the built app can actually run.
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)

# --- Dear ImGui (immediate-mode GUI) ---
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)
if(WIN32)
    target_link_libraries(imgui PUBLIC opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(imgui PUBLIC OpenGL::GL)
endif()
endif() # UECP_BUILD_GUI

# --- miniaudio (single-header audio I/O library) ---
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(miniaudio)

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${miniaudio_SOURCE_DIR})
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
