// ── Integration test conventions ────────────────────────────────
//
// Integration tests live in tests/integration/<name>_test.cpp and
// exercise SEAMS BETWEEN MULTIPLE MODULES (or between Kuma and an
// external tool like kuma-bake). They are headless: no SDL window,
// no Vulkan device, no audio, no GPU upload.
//
// Each test uses the helpers in tests/integration/test_helpers.h
// to spawn subprocesses (e.g. kuma-bake), capture exit codes, and
// reach in-tree fixtures via tests/fixtures/<area>/<file>.
//
// Tests are tagged with the "integration" CTest label so they can
// be run with `ctest -L integration` (and skipped via `ctest -L
// unit` for fast iteration).

#pragma once

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace kuma::integration {

// Returns the absolute path of a fixture under tests/fixtures/.
// KUMA_FIXTURES_DIR is defined by the test build via add_compile_definitions
// to the absolute path of tests/fixtures/ at configure time.
inline std::filesystem::path fixture(const std::string& relative) {
    return std::filesystem::path(KUMA_FIXTURES_DIR) / relative;
}

// Returns the absolute path to the kuma-bake executable, baked
// into the test build via KUMA_BAKE_EXE_PATH at configure time.
inline std::filesystem::path kuma_bake_exe() {
    return std::filesystem::path(KUMA_BAKE_EXE_PATH);
}

// Spawn kuma-bake with the given args, wait for completion, return
// its exit code. Output is inherited from the test process so any
// "kuma-bake: error ..." lines show up in the test log.
//
// Windows std::system runs through cmd.exe, which strips one outer
// pair of quotes. We wrap the whole command in an extra set of
// quotes so the inner quotes around the exe path and args survive.
inline int run_kuma_bake(const std::vector<std::string>& args) {
    std::string cmd = "\"\"" + kuma_bake_exe().string() + "\"";
    for (const auto& arg : args) {
        cmd += " \"" + arg + "\"";
    }
    cmd += "\"";
    return std::system(cmd.c_str());
}

// Read a file fully into a byte vector. Used to peek at .kmesh /
// .ktex outputs of kuma-bake without going through any engine
// loader.
inline std::vector<uint8_t> read_binary(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

}  // namespace kuma::integration
