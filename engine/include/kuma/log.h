#pragma once

// ── Kuma Logging System ─────────────────────────────────────────
// Usage:
//   kuma::log::info("Swapchain created: %ux%u", width, height);
//   kuma::log::warn("Texture not found: %s", path);
//   kuma::log::error("Failed to create pipeline");
//   kuma::log::trace("Frame %u acquired image %u", frame, image);
//
// Severity levels: trace < info < warn < error
// Trace is stripped in release builds via KUMA_LOG_LEVEL.

namespace kuma::log {

enum class Level {
    trace,
    info,
    warn,
    error
};

// Set the minimum level — messages below this are ignored.
// Default is trace (show everything) in debug, info in release.
void set_level(Level level);

// Core logging functions. Use these throughout the engine.
// Format strings use printf-style specifiers (%s, %d, %u, %f, etc.)
void trace(const char* fmt, ...);
void info(const char* fmt, ...);
void warn(const char* fmt, ...);
void error(const char* fmt, ...);

} // namespace kuma::log
