// ── kuma::time forwarders ───────────────────────────────────────
// Thin layer that owns the engine's single Clock instance and is
// the only place std::chrono touches the time system. The Clock
// kernel itself takes raw u64 nanosecond timestamps, which keeps
// it trivially testable.
//
// steady_clock — NOT system_clock — is mandatory here: system_clock
// is the wall clock and can go backwards (NTP sync, DST, user
// changes the time). A negative dt would underflow our u64 math
// and report a delta of ~584 years. steady_clock is the OS's
// monotonic counter; only goes forward, ever.

#include <kuma/time.h>

#include "platform/time_internal.h"

#include <chrono>

namespace kuma {

static Clock s_clock;

namespace time {

// Phase 2 of the frame contract — called from engine::begin_frame
// after window.poll_events. Not part of the public API; game code
// must not call this directly.
void tick() {
    const auto now = std::chrono::steady_clock::now();
    const auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         now.time_since_epoch())
                         .count();
    s_clock.tick(static_cast<uint64_t>(ns));
}

void reset() {
    s_clock.reset();
}

float    delta()       { return s_clock.delta_sec(); }
double   total()       { return s_clock.total_sec(); }
uint64_t frame_count() { return s_clock.frame_count(); }

}  // namespace time
}  // namespace kuma
