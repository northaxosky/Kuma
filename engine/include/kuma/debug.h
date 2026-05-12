#pragma once

// ── Kuma Debug Overlay ──────────────────────────────────────────
// Toggleable in-engine debug UI built on Dear ImGui. Game code uses
// this for runtime stats (FPS, frame time, custom panels). Default
// hotkey to toggle visibility is F3.
//
// Usage (game code):
//
//   if (kuma::debug::is_visible()) {
//       kuma::debug::draw_default_panel();   // FPS + frame time + 1% low
//       ImGui::Begin("Game stats");          // user-defined panel
//       ImGui::Text("Entities: %zu", count);
//       ImGui::End();
//   }
//
// Custom panels use ImGui directly (#include <imgui.h>). See ADR
// 0006-style "user owns the UI" decision in this module's notes.

#include <kuma/input.h>

#include <cstddef>

namespace kuma::debug {

// ── Visibility ─────────────────────────────────────────────────

void set_visible(bool visible);
bool is_visible();
void toggle();

// Default toggle key is F3. Pass Key::Count to disable the
// auto-listener entirely (useful if game code wants its own
// binding via debug::toggle()).
void set_toggle_key(Key key);

// ── Stats accessors (engine-computed) ──────────────────────────

// Smoothed FPS - exponential moving average over ~1 second.
float fps();

// Smoothed frame time in milliseconds.
float frame_time_ms();

// Average of the worst 1% of recently-recorded frames, in ms.
// The "stutter" stat - average FPS hides this; 1% low surfaces it.
float one_percent_low_ms();

// Returns the raw frame-time ring buffer (most-recent-last) for
// plotting. The pointer is valid until the next new_frame() call.
const float* frame_time_history(std::size_t* out_count);

// ── Default panel ──────────────────────────────────────────────
// Renders the standard FPS / frame time / 1% low / sparkline panel.
// Call from Phase 3 UPDATE if you want the default look. User code
// can also write its own panel using raw ImGui:: calls.
void draw_default_panel();

// Renders ImGui's built-in demo window. Useful as live reference
// docs while learning ImGui's API. Call from Phase 3 UPDATE.
void show_imgui_demo();

}  // namespace kuma::debug
