// ── Platform Paths ──────────────────────────────────────────────
// SDL3-backed exe-relative path resolution.

#include "platform/paths.h"

#include <SDL3/SDL.h>

namespace kuma::platform {

std::string exe_relative(const char* relative_path) {
    // SDL3: returns a const char* owned by SDL — do NOT free it.
    // SDL caches the result internally. Path always ends with a
    // platform separator (`\` on Windows, `/` on POSIX), so simple
    // concatenation is safe.
    const char* base = SDL_GetBasePath();
    if (!base) {
        return relative_path;
    }
    return std::string(base) + relative_path;
}

}  // namespace kuma::platform
