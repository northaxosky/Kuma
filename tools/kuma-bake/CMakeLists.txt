# ── kuma-bake ─────────────────────────────────────────────────────
# Rust binary that converts artist-friendly source assets (.obj,
# .png, ...) into the engine's runtime binary format (.kmesh, .ktex).
# The engine never calls Rust at runtime - it only reads the binary
# files this tool produces. See assets/source/ -> build/assets/.
#
# Cargo handles its own incremental builds, so re-invoking this is
# essentially free when nothing has changed.

set(KUMA_BAKE_TARGET_DIR ${CMAKE_BINARY_DIR}/tools/kuma-bake)
set(KUMA_BAKE_EXE ${KUMA_BAKE_TARGET_DIR}/release/kuma-bake.exe)

add_custom_target(kuma-bake-build ALL
    COMMAND ${CARGO_EXECUTABLE} build --release
            --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
            --target-dir ${KUMA_BAKE_TARGET_DIR}
    BYPRODUCTS ${KUMA_BAKE_EXE}
    COMMENT "Building kuma-bake (Rust)"
    USES_TERMINAL
)

# Helper functions consumed by sandbox / future game targets to
# bake individual assets. Each call adds a custom command that
# produces a .kmesh or .ktex; the calling target lists those
# outputs in its dependencies.

function(kuma_bake_mesh INPUT_OBJ)
    get_filename_component(BASE ${INPUT_OBJ} NAME_WE)
    set(OUTPUT ${CMAKE_BINARY_DIR}/assets/models/${BASE}.kmesh)
    add_custom_command(
        OUTPUT ${OUTPUT}
        COMMAND ${KUMA_BAKE_EXE} mesh ${INPUT_OBJ} ${OUTPUT}
        DEPENDS ${INPUT_OBJ} kuma-bake-build
        COMMENT "Baking mesh: ${BASE}.obj -> ${BASE}.kmesh"
    )
    set(KUMA_BAKED_${BASE} ${OUTPUT} PARENT_SCOPE)
endfunction()

function(kuma_bake_texture INPUT_IMG)
    get_filename_component(BASE ${INPUT_IMG} NAME_WE)
    set(OUTPUT ${CMAKE_BINARY_DIR}/assets/textures/${BASE}.ktex)
    add_custom_command(
        OUTPUT ${OUTPUT}
        COMMAND ${KUMA_BAKE_EXE} tex ${INPUT_IMG} ${OUTPUT}
        DEPENDS ${INPUT_IMG} kuma-bake-build
        COMMENT "Baking texture: ${BASE} -> ${BASE}.ktex"
    )
    set(KUMA_BAKED_${BASE} ${OUTPUT} PARENT_SCOPE)
endfunction()
