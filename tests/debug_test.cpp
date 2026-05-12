// Tests for kuma::debug stats math (FPS smoothing, ring buffer,
// 1% low). Reaches into the test-only detail:: hooks so we can
// drive the math without Vulkan or ImGui being initialized.
//
// The visibility/toggle/process_event/render paths are integration
// concerns that need a live ImGui context, which we cannot stand
// up in a headless test. Those are covered by the sandbox smoke run.

#include <kuma/debug.h>

#include "core/debug_test_hooks.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

using kuma::debug::detail::debug_record_frame_sample_for_test;
using kuma::debug::detail::debug_reset_stats_for_test;

// Each test starts with a clean slate.
class DebugStats : public ::testing::Test {
protected:
    void SetUp() override { debug_reset_stats_for_test(); }
};

}  // namespace

TEST_F(DebugStats, FreshStatsReportZero) {
    EXPECT_FLOAT_EQ(kuma::debug::fps(), 0.0f);
    EXPECT_FLOAT_EQ(kuma::debug::frame_time_ms(), 0.0f);
    EXPECT_FLOAT_EQ(kuma::debug::one_percent_low_ms(), 0.0f);

    std::size_t count = 0;
    kuma::debug::frame_time_history(&count);
    EXPECT_EQ(count, 0u);
}

TEST_F(DebugStats, FirstSampleBootstrapsSmoothedFrameTime) {
    debug_record_frame_sample_for_test(16.667f);
    // The very first sample bypasses EMA blending and is taken as-is.
    EXPECT_NEAR(kuma::debug::frame_time_ms(), 16.667f, 1e-3f);
}

TEST_F(DebugStats, FpsIsReciprocalOfSmoothedFrameTime) {
    debug_record_frame_sample_for_test(16.667f);  // ~60 FPS
    EXPECT_NEAR(kuma::debug::fps(), 1000.0f / 16.667f, 1e-2f);
}

TEST_F(DebugStats, RingBufferGrowsThenStaysBounded) {
    constexpr std::size_t kCapacity = 120;

    // Fill exactly to capacity
    for (std::size_t i = 0; i < kCapacity; ++i) {
        debug_record_frame_sample_for_test(static_cast<float>(i + 1));
    }
    std::size_t count = 0;
    kuma::debug::frame_time_history(&count);
    EXPECT_EQ(count, kCapacity);

    // Push past capacity - count must stay at capacity
    for (std::size_t i = 0; i < 50; ++i) {
        debug_record_frame_sample_for_test(999.0f);
    }
    kuma::debug::frame_time_history(&count);
    EXPECT_EQ(count, kCapacity);
}

TEST_F(DebugStats, RingBufferLinearizesOldestFirst) {
    // Fill with a known sequence past wrap-around so we exercise the
    // (head + i) % capacity path.
    constexpr std::size_t kCapacity = 120;
    for (std::size_t i = 0; i < kCapacity + 10; ++i) {
        debug_record_frame_sample_for_test(static_cast<float>(i));
    }

    std::size_t count = 0;
    const float* hist = kuma::debug::frame_time_history(&count);
    ASSERT_EQ(count, kCapacity);
    ASSERT_NE(hist, nullptr);

    // Oldest-first: after writing 0..129, the buffer holds 10..129.
    EXPECT_FLOAT_EQ(hist[0], 10.0f);
    EXPECT_FLOAT_EQ(hist[kCapacity - 1], 129.0f);
}

TEST_F(DebugStats, OnePercentLowIsAverageOfWorstSamples) {
    // 100 samples of 16ms with one 100ms spike.
    // 1% of 100 = 1 sample, so the 1% low is just the spike.
    for (int i = 0; i < 99; ++i) debug_record_frame_sample_for_test(16.0f);
    debug_record_frame_sample_for_test(100.0f);
    EXPECT_NEAR(kuma::debug::one_percent_low_ms(), 100.0f, 1e-3f);
}

TEST_F(DebugStats, OnePercentLowFloorsAtOneSampleEvenWithFewSamples) {
    // 50 samples - 50/100 = 0, but we floor at 1 so it returns the
    // single worst frame instead of dividing by zero.
    for (int i = 0; i < 49; ++i) debug_record_frame_sample_for_test(10.0f);
    debug_record_frame_sample_for_test(99.0f);
    EXPECT_NEAR(kuma::debug::one_percent_low_ms(), 99.0f, 1e-3f);
}

TEST_F(DebugStats, EmaSmoothingTracksSteadyState) {
    // After many identical samples the EMA must converge to that value.
    for (int i = 0; i < 200; ++i) debug_record_frame_sample_for_test(8.0f);
    EXPECT_NEAR(kuma::debug::frame_time_ms(), 8.0f, 1e-3f);
    EXPECT_NEAR(kuma::debug::fps(), 125.0f, 1e-2f);
}

TEST_F(DebugStats, ResetClearsEverything) {
    debug_record_frame_sample_for_test(16.667f);
    debug_record_frame_sample_for_test(33.333f);
    debug_reset_stats_for_test();

    std::size_t count = 0;
    kuma::debug::frame_time_history(&count);
    EXPECT_EQ(count, 0u);
    EXPECT_FLOAT_EQ(kuma::debug::fps(), 0.0f);
    EXPECT_FLOAT_EQ(kuma::debug::frame_time_ms(), 0.0f);
}

// ── Visibility ──────────────────────────────────────────────────

TEST(DebugVisibility, DefaultsToHidden) {
    EXPECT_FALSE(kuma::debug::is_visible());
}

TEST(DebugVisibility, ToggleFlipsState) {
    const bool before = kuma::debug::is_visible();
    kuma::debug::toggle();
    EXPECT_NE(before, kuma::debug::is_visible());
    kuma::debug::toggle();  // restore
    EXPECT_EQ(before, kuma::debug::is_visible());
}

TEST(DebugVisibility, SetVisibleForcesValue) {
    kuma::debug::set_visible(true);
    EXPECT_TRUE(kuma::debug::is_visible());
    kuma::debug::set_visible(false);
    EXPECT_FALSE(kuma::debug::is_visible());
}
