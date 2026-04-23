// ── Kuma Logging Implementation ─────────────────────────────────
// Colored console output using ANSI escape codes.

#include <kuma/log.h>

#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

namespace kuma::log {

// ── State ───────────────────────────────────────────────────────

#ifdef NDEBUG
static Level s_min_level = Level::Info;  // release: skip trace
#else
static Level s_min_level = Level::Trace;  // debug: show everything
#endif

// ── ANSI Color Codes ────────────────────────────────────────────
// These work on Windows 10+ and all modern terminals.
//
//   \033[  = escape sequence start
//   90     = bright black (gray)     — trace
//   37     = white                   — info
//   33     = yellow                  — warn
//   31     = red                     — error
//   0m     = reset to default

static constexpr const char* COLOR_RESET = "\033[0m";
static constexpr const char* COLOR_TRACE = "\033[90m";  // gray
static constexpr const char* COLOR_INFO = "\033[37m";   // white
static constexpr const char* COLOR_WARN = "\033[33m";   // yellow
static constexpr const char* COLOR_ERROR = "\033[31m";  // red

// ── Enable ANSI on Windows ──────────────────────────────────────
// Windows needs virtual terminal processing enabled for ANSI codes.
// This runs once at startup via a static initializer.

#ifdef _WIN32
static bool enable_ansi_colors() {
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (console == INVALID_HANDLE_VALUE)
        return false;

    DWORD mode = 0;
    if (!GetConsoleMode(console, &mode))
        return false;

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    return SetConsoleMode(console, mode) != 0;
}

static bool s_ansi_enabled = enable_ansi_colors();
#endif

// ── Real-time stdout (debug builds only) ────────────────────────
// When stdout is not a TTY (VS Code debug console, redirected to a
// file, piped to another process), the C runtime defaults to block
// buffering — so logs only appear in chunks of ~4 KB or at exit.
//
// Switch to unbuffered so every printf hits the console immediately.
// Gated on !NDEBUG so release builds keep the default (faster)
// buffering. NDEBUG is auto-defined by CMake in Release builds.
//
// _IONBF (no buffering) is used instead of _IOLBF (line) because
// MSVC requires a non-zero buffer size for _IOLBF; with _IONBF the
// buffer/size arguments are genuinely ignored.

#ifndef NDEBUG
static bool enable_realtime_stdout() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    return true;
}

static bool s_realtime_stdout = enable_realtime_stdout();
#endif

// ── Core Implementation ─────────────────────────────────────────

static void log_message(Level level, const char* fmt, va_list args) {
    if (level < s_min_level)
        return;

    const char* color = COLOR_INFO;
    const char* label = "info";

    switch (level) {
    case Level::Trace:
        color = COLOR_TRACE;
        label = "Trace";
        break;
    case Level::Info:
        color = COLOR_INFO;
        label = "Info";
        break;
    case Level::Warn:
        color = COLOR_WARN;
        label = "Warn";
        break;
    case Level::Error:
        color = COLOR_ERROR;
        label = "Error";
        break;
    }

    // Print: [Kuma][level] message
    std::printf("%s[Kuma][%s] ", color, label);
    std::vprintf(fmt, args);
    std::printf("%s\n", COLOR_RESET);
}

// ── Public API ──────────────────────────────────────────────────

void set_level(Level level) {
    s_min_level = level;
}

void trace(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(Level::Trace, fmt, args);
    va_end(args);
}

void info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(Level::Info, fmt, args);
    va_end(args);
}

void warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(Level::Warn, fmt, args);
    va_end(args);
}

void error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(Level::Error, fmt, args);
    va_end(args);
}

}  // namespace kuma::log
