# ── Kuma Engine Library ──────────────────────────────────────────
# Builds as a static library. Game executables link against this.

add_library(kuma-engine STATIC)

# Source files — add new .cpp files here as we build systems
target_sources(kuma-engine PRIVATE
    src/core/engine.cpp
    src/platform/window.cpp
)

# Public headers: anyone linking kuma-engine gets include/kuma/ on their include path
# This means game code can write: #include <kuma/kuma.h>
target_include_directories(kuma-engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# SDL3 — PRIVATE because game code should never use SDL directly
target_link_libraries(kuma-engine PRIVATE SDL3::SDL3)

# Compiler warnings — treat warnings seriously from the start
target_compile_options(kuma-engine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)
