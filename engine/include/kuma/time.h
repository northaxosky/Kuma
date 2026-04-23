#pragma once

// ── kuma::time ──────────────────────────────────────────────────
// Frame timing — how long the last frame took, how long the game
// has been running, what frame number we're on.
//
// Phase 2 of the frame contract (see kuma.h). Values become valid
// after the first call to kuma::begin_frame() and update once per
// frame thereafter. Game code should not call any of these from
// init() — the clock has not ticked yet, all queries return 0.

#include <cstdint>

namespace kuma::time {

// Seconds elapsed since the previous frame, clamped to 100ms to
// guard against debugger pauses and frame hitches. Always 0 on
// the very first frame (no previous frame to measure against).
//
// Use for per-frame motion: position += velocity * delta()
float delta();

// Seconds elapsed since the engine started ticking. Sum of every
// dt the game has actually seen (so it advances by the clamped
// value, not wall-clock time, after a long stall). double for
// drift resistance — float would lose sub-millisecond precision
// after ~10 minutes of uptime.
double total();

// Number of frames completed since startup. Starts at 0, becomes
// 1 after the first tick. Useful for "every N frames" log throttling
// and for warming up systems that need a few frames before they're
// stable.
uint64_t frame_count();

}  // namespace kuma::time
