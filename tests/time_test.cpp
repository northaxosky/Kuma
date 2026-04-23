// Tests for kuma::Clock, the testable kernel of the time module.
//
// We test the struct directly (not the kuma::time:: namespace forwarders)
// so each test gets its own isolated state — no static-state leakage, no
// chrono dependency, no engine lifecycle. The forwarders just sample
// std::chrono::steady_clock and call into Clock; that one-line seam isn't
// worth testing separately.
//
// Pattern: construct a Clock, call tick() with hand-picked u64 nanosecond
// timestamps, assert on the resulting delta/total/frame_count.

#include "platform/time_internal.h"

#include <gtest/gtest.h>

#include <cstdint>

using kuma::Clock;

namespace {

// Readability helpers — all timestamps in this file are in nanoseconds.
constexpr uint64_t kMs = 1'000'000ull;
constexpr uint64_t kSec = 1'000'000'000ull;

// Arbitrary "starting wall time" — a real steady_clock value is some big
// number, so we deliberately don't start at 0. Tests should not depend on
// the absolute timestamp, only on the deltas between them.
constexpr uint64_t kT0 = 1'000'000'000'000ull;  // 1000 seconds in ns

}  // namespace

// ── Initial state ───────────────────────────────────────────────────

TEST(Clock, FreshClockReadsAllZero) {
    Clock c;
    EXPECT_FLOAT_EQ(c.delta_sec(), 0.0f);
    EXPECT_DOUBLE_EQ(c.total_sec(), 0.0);
    EXPECT_EQ(c.frame_count(), 0u);
}

// ── First tick is special ───────────────────────────────────────────

TEST(Clock, FirstTickReportsZeroDeltaAndStartsFrameCount) {
    Clock c;
    c.tick(kT0);

    // No previous frame to measure against — dt must be exactly 0,
    // never a fabricated default. total stays at 0 for the same
    // reason. frame_count goes from 0 → 1.
    EXPECT_FLOAT_EQ(c.delta_sec(), 0.0f);
    EXPECT_DOUBLE_EQ(c.total_sec(), 0.0);
    EXPECT_EQ(c.frame_count(), 1u);
}

// ── Normal ticks ────────────────────────────────────────────────────

TEST(Clock, SecondTickComputesDeltaFromTimestampDifference) {
    Clock c;
    c.tick(kT0);
    c.tick(kT0 + 16 * kMs);  // ~60 FPS frame

    EXPECT_NEAR(c.delta_sec(), 0.016f, 1e-6f);
    EXPECT_NEAR(c.total_sec(), 0.016, 1e-9);
    EXPECT_EQ(c.frame_count(), 2u);
}

TEST(Clock, FrameCountIncrementsByOnePerTick) {
    Clock c;
    for (int i = 0; i < 5; ++i) {
        c.tick(kT0 + static_cast<uint64_t>(i) * 16 * kMs);
    }
    EXPECT_EQ(c.frame_count(), 5u);
}

TEST(Clock, TotalAccumulatesAcrossManyTicks) {
    Clock c;
    c.tick(kT0);
    c.tick(kT0 + 10 * kMs);
    c.tick(kT0 + 25 * kMs);
    c.tick(kT0 + 40 * kMs);

    // Total is the sum of the 3 measured deltas: 10 + 15 + 15 = 40 ms.
    EXPECT_NEAR(c.total_sec(), 0.040, 1e-9);
    EXPECT_EQ(c.frame_count(), 4u);
}

// ── Clamping ────────────────────────────────────────────────────────

TEST(Clock, DeltaClampsAtMaxOnLongStall) {
    Clock c;
    c.tick(kT0);
    c.tick(kT0 + 5 * kSec);  // simulate 5-second debugger pause

    // Reported dt is the cap, not the real elapsed time. Anti-spiral.
    EXPECT_NEAR(c.delta_sec(), 0.1f, 1e-6f);
    EXPECT_NEAR(c.total_sec(), 0.1, 1e-9);
}

// CRITICAL test — the "advance from real time" rule. After a clamped
// tick, the next tick must measure from the *actual* previous timestamp
// (kT0 + 5s), NOT from the clamped position. Otherwise every clamp
// would leave a 4.9-second residual that bleeds into the next frame.
TEST(Clock, ClampDoesNotPoisonNextFrameDelta) {
    Clock c;
    c.tick(kT0);
    c.tick(kT0 + 5 * kSec);          // big stall, dt clamped to 100ms
    c.tick(kT0 + 5 * kSec + 16 * kMs);  // 16ms after the stall ended

    // If last_tick_ns_ had been advanced by the clamped 100ms instead
    // of the real 5s, this delta would be ~4.9s, not 16ms.
    EXPECT_NEAR(c.delta_sec(), 0.016f, 1e-6f);
}

// ── Reset ───────────────────────────────────────────────────────────

TEST(Clock, ResetClearsEverythingIncludingStartedFlag) {
    Clock c;
    c.tick(kT0);
    c.tick(kT0 + 100 * kMs);
    EXPECT_EQ(c.frame_count(), 2u);

    c.reset();

    EXPECT_FLOAT_EQ(c.delta_sec(), 0.0f);
    EXPECT_DOUBLE_EQ(c.total_sec(), 0.0);
    EXPECT_EQ(c.frame_count(), 0u);

    // After reset, the next tick should behave as a true first tick:
    // dt=0, frame_count=1. If started_ hadn't been cleared, this would
    // instead compute a huge dt from the stale last_tick_ns_.
    c.tick(kT0 + 10 * kSec);
    EXPECT_FLOAT_EQ(c.delta_sec(), 0.0f);
    EXPECT_EQ(c.frame_count(), 1u);
}
