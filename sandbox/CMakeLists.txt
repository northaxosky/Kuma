# ── Sandbox Application ──────────────────────────────────────────
# A minimal app that links against the Kuma engine.
# This is our "does the engine actually work?" test bed.

add_executable(sandbox
    src/main.cpp
)

# Link against the engine — this automatically gives us the include paths
# we marked as PUBLIC in the engine's CMakeLists.txt
target_link_libraries(sandbox PRIVATE kuma-engine)
