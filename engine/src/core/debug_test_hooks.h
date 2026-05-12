#pragma once

// Test-only hooks for the kuma::debug stats math. Lives in its own
// tiny header (no Vulkan / SDL / ImGui includes) so unit tests can
// drive the pure logic without standing up the full graphics stack.
//
// Production code MUST NOT include this header.

namespace kuma::debug::detail {

// Append a frame-time sample (ms) to the internal ring buffer +
// EMA. In production this is called from new_frame() with the
// previous frame's delta.
void debug_record_frame_sample_for_test(float frame_ms);

// Wipe the ring buffer + EMA back to fresh-init state. Used by
// SetUp() so each test runs against a clean slate.
void debug_reset_stats_for_test();

// Override the `initialized` flag so unit tests can flip stats
// queries on without going through Vulkan init. Currently unused
// by tests but kept for symmetry / future debug paths.
void debug_set_initialized_for_test(bool v);

}  // namespace kuma::debug::detail
