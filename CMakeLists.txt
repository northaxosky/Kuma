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

# ── Jolt Physics ──────────────────────────────────────────────────
# Jorrit Rouwé's MIT-licensed engine, used by Horizon Forbidden West
# and Godot 4. Disable the in-tree samples / tests / install rules so
# only the core Jolt static library gets built. SOURCE_SUBDIR points
# at Build/ because Jolt's CMakeLists lives there, not at repo root.
set(TARGET_UNIT_TESTS         OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD        OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST   OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES            OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER             OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL            OFF CACHE BOOL "" FORCE)
# Jolt defaults this to ON which makes its build inject -Werror style
# flags. We keep our own warning policy in engine/CMakeLists.txt.
set(ENABLE_ALL_WARNINGS       OFF CACHE BOOL "" FORCE)
# Jolt defaults this to ON which would clobber the parent project's
# CMAKE_CXX_FLAGS_DEBUG/RELEASE. Hands off our build flags.
set(OVERRIDE_CXX_FLAGS        OFF CACHE BOOL "" FORCE)
# LTO on Jolt slows debug iteration; flip ON later if profiling
# shows the physics step is hot.
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
# Match the parent project's MSVC CRT (we use the dynamic /MDd by
# default). Jolt defaults to /MT static which causes LNK2038 mismatch
# errors when linked into a /MD consumer.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.5.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)

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
