#pragma once

// ── kuma::platform ──────────────────────────────────────────────
// Launcher-independent path resolution and other platform utilities.

#include <string>

namespace kuma::platform {

// Returns an absolute path built from the running executable's
// directory and the given relative path. Works regardless of the
// process's current working directory, which matters because
// VS Code's F5, `ctest`, and a double-click from Explorer all hand
// the engine a different CWD.
//
// Requires SDL to be initialized (kuma::init handles this before
// any callsite runs).
std::string exe_relative(const char* relative_path);

}  // namespace kuma::platform
