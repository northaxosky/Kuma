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

# Bake source assets into the engine binary format. Each helper
# adds a custom command that runs kuma-bake whenever the source
# file changes; the outputs land in build/assets/{models,textures,sounds,scenes}/.
kuma_bake_mesh(${CMAKE_SOURCE_DIR}/assets/source/models/quad.obj)
kuma_bake_gltf(${CMAKE_SOURCE_DIR}/assets/source/models/icosahedron.glb)
kuma_bake_texture(${CMAKE_SOURCE_DIR}/assets/source/textures/VaultBoyNV.png)
kuma_bake_sound(${CMAKE_SOURCE_DIR}/assets/source/sounds/thud.wav)
kuma_bake_sound(${CMAKE_SOURCE_DIR}/assets/source/sounds/ambient.wav)

# Sponza glTF is gitignored - run tools/download_sponza.ps1 once
# per checkout to fetch it. Sandbox builds without Sponza too;
# the scene::load_and_spawn call just logs a warning and the player
# runs in an empty space with the physics floor only.
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/source/scenes/Sponza/Sponza.gltf")
    kuma_bake_scene(${CMAKE_SOURCE_DIR}/assets/source/scenes/Sponza/Sponza.gltf)
else()
    message(STATUS "Sponza source not found; sandbox will run without a level. "
                   "Fetch via tools/download_sponza.ps1.")
endif()

# Roll every baked asset into a single ALL target so they all
# build as part of cmake --build, then make sandbox depend on it.
get_property(_baked_files GLOBAL PROPERTY KUMA_BAKED_FILES)
add_custom_target(kuma-baked-assets ALL DEPENDS ${_baked_files})
add_dependencies(sandbox kuma-baked-assets)

# Copy the BAKED assets next to the executable.
add_custom_command(TARGET sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/assets
        $<TARGET_FILE_DIR:sandbox>/assets
    COMMENT "Copying baked assets to sandbox output directory"
)
