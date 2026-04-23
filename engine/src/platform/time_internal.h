#pragma once

// ── Time Internal ───────────────────────────────────────────────
// Non-public header for the time system. Lives in src/ (not in
// include/kuma/) so it's reachable from other engine .cpp files
// and tests, but game code never sees it.
//
// The kernel is SDL-free AND chrono-free by design: it takes a
// raw nanosecond timestamp and does pure integer math. The seam
// to std::chrono::steady_clock lives one layer up in core/time.cpp,
// which lets tests drive Clock with any u64 sequence they like.

#include <algorithm>
#include <cstdint>

namespace kuma {

// ── Clock ───────────────────────────────────────────────────────
// Frame timing state. One instance lives inside the engine; tests
// construct their own for unit testing without engine lifecycle.
//
// Phase 2 of the frame contract (see kuma.h): tick() runs after
// input is polled, before game UPDATE reads time::delta().
struct Clock {
    // ── Tunables ────────────────────────────────────────────────
    // Anti-spiral-of-death dt cap. If a frame stalls (debugger
    // pause, hitch, GC), we lie to gameplay and say it was 100ms
    // instead of the real 5s. Physics accumulators stay sane.
    // 100ms is the standard value (Unity, Unreal use similar caps).
    static constexpr uint64_t kMaxDtNs = 100'000'000ull;

    // ── State ───────────────────────────────────────────────────
    // float delta: small range, multiplied with Vec3 every frame,
    // 32-bit GPU constants — float is the right type at point of use.
    float delta_sec_ = 0.0f;

    // double total: drift-resistant. After ~10 min uptime a float
    // accumulator starts losing sub-millisecond precision. Unreal
    // makes the same delta-vs-total split for the same reason.
    double total_sec_ = 0.0;

    // u64 frame count: starts at 0, becomes 1 after the first tick.
    // 64-bit because at 144 FPS a u32 wraps after ~10 months — fine
    // for a game session, but free to just use u64 and never think
    // about it.
    uint64_t frame_count_ = 0;

    // ── Bookkeeping (internal, not exposed) ─────────────────────
    uint64_t last_tick_ns_ = 0;
    bool     started_      = false;

    // ── tick ────────────────────────────────────────────────────
    // Advance the clock by sampling the given timestamp. The first
    // tick seeds last_tick_ns_ and reports dt=0 — there's no
    // "previous frame" to measure against, and lying about it
    // (e.g. defaulting to 1/60s) corrupts gameplay reasoning.
    void tick(uint64_t now_ns) {
        if (!started_) {
            last_tick_ns_ = now_ns;
            started_      = true;
            frame_count_  = 1;
            // delta_sec_ and total_sec_ stay at 0.
            return;
        }

        const uint64_t raw_dt_ns = now_ns - last_tick_ns_;
        const uint64_t clamped   = std::min(raw_dt_ns, kMaxDtNs);

        delta_sec_   = static_cast<float>(static_cast<double>(clamped) / 1e9);
        total_sec_  += static_cast<double>(clamped) / 1e9;
        frame_count_ += 1;

        // ALWAYS advance from real time, even after clamping. If we
        // advanced from `last_tick_ns_ + clamped` instead, the clock
        // would lag wall time forever after every long stall.
        last_tick_ns_ = now_ns;
    }

    // ── Queries ─────────────────────────────────────────────────
    float    delta_sec()   const { return delta_sec_; }
    double   total_sec()   const { return total_sec_; }
    uint64_t frame_count() const { return frame_count_; }

    // Zero all state. Mainly for tests.
    void reset() {
        delta_sec_    = 0.0f;
        total_sec_    = 0.0;
        frame_count_  = 0;
        last_tick_ns_ = 0;
        started_      = false;
    }
};

}  // namespace kuma
