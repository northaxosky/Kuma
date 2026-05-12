cmake_minimum_required(VERSION 3.24)
project(Kuma
    VERSION 0.1.0
    DESCRIPTION "Lightweight game engine"
    LANGUAGES CXX
)

# ── Global Settings ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build output goes to predictable locations
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── Options ──────────────────────────────────────────────────────
option(KUMA_BUILD_SANDBOX "Build the sandbox test application" ON)
option(KUMA_BUILD_TESTS   "Build the unit tests" ON)

# ── Dependencies ─────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.14
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL3)

find_package(Vulkan REQUIRED)

# ── Rust toolchain (kuma-bake) ───────────────────────────────────
# kuma-bake is the offline asset baker that produces the engine's
# binary asset format (.kmesh, .ktex). The engine has no runtime
# loaders for source formats - if it's not baked, it can't load.
# Hard-required.
find_program(CARGO_EXECUTABLE cargo)
if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR
        "Cargo (Rust) not found - required to build kuma-bake. "
        "Install Rust >= 1.85 from https://rustup.rs and re-run cmake.")
endif()

# ── Dear ImGui ────────────────────────────────────────────────────
# Used by the debug overlay (kuma::debug). ImGui doesn't ship a
# CMakeLists.txt, so we populate the source and define our own
# target with the core sources + SDL3 + Vulkan backends.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)

# ── Sub-projects ─────────────────────────────────────────────────
add_subdirectory(engine)
add_subdirectory(tools/kuma-bake)

if(KUMA_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

if(KUMA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
