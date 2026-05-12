# ── Unit Tests ───────────────────────────────────────────────────
# GoogleTest pulled in via FetchContent — same pattern as SDL3 in the root
# CMakeLists.txt. Keeps dependency management consistent and means a fresh
# clone just works without a separate `apt-get install` / vcpkg step.

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)

# On Windows, GoogleTest defaults to linking the static CRT which mismatches
# our engine's dynamic CRT and causes LNK2038. This forces GTest to use the
# shared CRT so everything links cleanly.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(kuma-tests
    math_test.cpp
    asset_format_test.cpp
    camera_test.cpp
    debug_test.cpp
    ecs_test.cpp
    input_test.cpp
    time_test.cpp
    transform_test.cpp
    integration/asset_pipeline_test.cpp
)

target_link_libraries(kuma-tests PRIVATE
    kuma-engine
    GTest::gtest_main
    SDL3::SDL3
)

# Tests reach into engine private headers (platform/input_internal.h, etc.)
# via the engine's src/ directory. Public game code never sees this path —
# only the test binary does.
target_include_directories(kuma-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/src
    ${CMAKE_CURRENT_SOURCE_DIR}/integration
)

# Integration tests need the kuma-bake exe and the on-disk fixtures
# folder. Burning these in at configure time means each test can
# call kuma::integration::fixture("...") / kuma_bake_exe() without
# searching at runtime.
target_compile_definitions(kuma-tests PRIVATE
    KUMA_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    KUMA_BAKE_EXE_PATH="${KUMA_BAKE_EXE}"
)

# Make the test binary depend on the kuma-bake exe so a fresh
# `cmake --build` always has a usable baker before tests run.
add_dependencies(kuma-tests kuma-bake-build)

# Same warning level as the engine — tests should be held to the same bar.
target_compile_options(kuma-tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# Auto-register each TEST() with CTest. Two passes tag the
# integration suite separately from unit so users can run
# `ctest -L unit` (fast) or `ctest -L integration` (slower
# subprocess-spawning suite). Convention: every integration test
# must use a fixture name starting with "Integration" - the filter
# relies on it. See tests/integration/test_helpers.h for details.
include(GoogleTest)
gtest_discover_tests(kuma-tests
    DISCOVERY_TIMEOUT 30
    TEST_FILTER "Integration*"
    PROPERTIES LABELS "integration"
)
gtest_discover_tests(kuma-tests
    DISCOVERY_TIMEOUT 30
    TEST_FILTER "-Integration*"
    PROPERTIES LABELS "unit"
)
