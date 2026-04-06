# ── Sandbox Application ──────────────────────────────────────────
# A minimal app that links against the Kuma engine.
# This is our "does the engine actually work?" test bed.

add_executable(sandbox
    src/main.cpp
)

# Link against the engine — this automatically gives us the include paths
# we marked as PUBLIC in the engine's CMakeLists.txt
target_link_libraries(sandbox PRIVATE kuma-engine)

# Copy compiled shaders next to the sandbox executable after each build.
# $<TARGET_FILE_DIR:sandbox> resolves to the actual output dir (e.g. bin/Debug/).
# This ensures "shaders/triangle.vert.spv" is found via relative path at runtime.
add_custom_command(TARGET sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${KUMA_SHADER_OUTPUT_DIR}
        $<TARGET_FILE_DIR:sandbox>/shaders
    COMMENT "Copying shaders to sandbox output directory"
)
