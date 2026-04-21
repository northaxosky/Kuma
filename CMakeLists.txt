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

# ── Sub-projects ─────────────────────────────────────────────────
add_subdirectory(engine)

if(KUMA_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

if(KUMA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
