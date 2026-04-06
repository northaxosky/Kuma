# ── Kuma Engine Library ──────────────────────────────────────────
# Builds as a static library. Game executables link against this.

add_library(kuma-engine STATIC)

# Source files — add new .cpp files here as we build systems
target_sources(kuma-engine PRIVATE
    src/core/engine.cpp
    src/platform/window.cpp
    src/renderer/renderer.cpp
    src/renderer/device.cpp
    src/renderer/swapchain.cpp
    src/renderer/pipeline.cpp
    src/renderer/resources.cpp
)

# Public headers: anyone linking kuma-engine gets include/kuma/ on their include path
# This means game code can write: #include <kuma/kuma.h>
target_include_directories(kuma-engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# SDL3 — PRIVATE because game code should never use SDL directly
target_link_libraries(kuma-engine PRIVATE SDL3::SDL3 Vulkan::Vulkan)

# Compiler warnings — treat warnings seriously from the start
target_compile_options(kuma-engine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# ── Shader Compilation ──────────────────────────────────────────
# glslc compiles GLSL (.vert, .frag) into SPIR-V (.spv) at build time.
# The Vulkan SDK provides glslc — Vulkan::glslc is the CMake target for it.
find_program(GLSLC_EXECUTABLE glslc HINTS Vulkan::glslc)

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)

# List every shader source file explicitly (no globbing — CMake best practice)
set(SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/triangle.vert
    ${SHADER_SOURCE_DIR}/triangle.frag
    ${SHADER_SOURCE_DIR}/quad.vert
    ${SHADER_SOURCE_DIR}/quad.frag
)

# For each shader, add a custom command that compiles it to SPIR-V.
# Compiled shaders go to build/shaders/ (a stable, config-independent location).
foreach(SHADER_SRC ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SRC} NAME)
    set(SHADER_SPV ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT  ${SHADER_SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER_SRC} -o ${SHADER_SPV}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling shader: ${SHADER_NAME} -> ${SHADER_NAME}.spv"
    )

    list(APPEND SHADER_SPV_FILES ${SHADER_SPV})
endforeach()

add_custom_target(kuma-shaders DEPENDS ${SHADER_SPV_FILES})
add_dependencies(kuma-engine kuma-shaders)

# Make compiled shaders available as a CMake variable for downstream targets.
# Executables linking kuma-engine can use this to copy shaders next to the exe.
set(KUMA_SHADER_OUTPUT_DIR ${SHADER_OUTPUT_DIR} PARENT_SCOPE)
