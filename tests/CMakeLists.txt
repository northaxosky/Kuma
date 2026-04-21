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
)

target_link_libraries(kuma-tests PRIVATE
    kuma-engine
    GTest::gtest_main
)

# Same warning level as the engine — tests should be held to the same bar.
target_compile_options(kuma-tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# Auto-register each TEST() with CTest so `ctest` picks them up individually.
include(GoogleTest)
gtest_discover_tests(kuma-tests)
