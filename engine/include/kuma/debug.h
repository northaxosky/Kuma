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
// Custom panels use ImGui directly (#include <imgui.h>) - the
// engine deliberately doesn't wrap ImGui's already-clean API.

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

// Monitor refresh rate (Hz) sampled at init. Used to derive the
// frame-time thresholds that drive the green/yellow/red coloring
// in the default panel. Returns 60.0f if the OS didn't report one.
float monitor_refresh_hz();

// ── Color helpers (used by the default panel; available for
// custom panels too) ───────────────────────────────────────────

// "Lower is better" thresholds for status_text. A measured value
// below `warn_above` is green; between warn and bad is yellow;
// above `bad_above` is red.
struct StatusThresholds {
    float warn_above;
    float bad_above;
};

// Build the standard frame-time thresholds from the current monitor
// refresh rate: warn when frame time exceeds 1.5x the ideal budget,
// bad when it exceeds 2.0x. The defaults respond to the user's
// monitor (16.7ms baseline at 60Hz, 6.9ms at 144Hz).
StatusThresholds frame_time_thresholds();

// Renders "label: <value>" where the value text is tinted red,
// yellow, or green based on `thresholds`. The format string applies
// to the value only and must consume exactly one float.
//
// Use case: status_text("FPS:", "%.1f", fps(), {30.0f, 15.0f}) -
// note that for inverted metrics like FPS the caller passes the
// inverted thresholds (lower is worse).
void status_text(const char* label, const char* fmt, float value,
                 StatusThresholds thresholds, bool higher_is_better = false);

// Tinted bold-style header for grouping a panel's sections.
// Renders in the engine's accent blue with a separator underneath.
void section_header(const char* text);

// ── Default panel ──────────────────────────────────────────────
// Renders the standard FPS / frame time / 1% low / sparkline panel.
// Call from your update loop if you want the default look. User
// code can also write its own panel using raw ImGui:: calls.
void draw_default_panel();

// Renders ImGui's built-in demo window. Useful as live reference
// docs while learning ImGui's API.
void show_imgui_demo();

}  // namespace kuma::debug
