#pragma once

// ── Kuma Input System ───────────────────────────────────────────
// Usage:
//   if (kuma::input::is_key_down(kuma::Key::W)) { ... }
//   if (kuma::input::was_key_pressed(kuma::Key::Space)) { ... }
//
// Key and MouseButton are layout-invariant, SDL-free identifiers.
// Translation to/from SDL scancodes lives in platform/input.cpp so
// public headers never pull in SDL.

#include <cstdint>

#include <kuma/math.h>

namespace kuma {

// ── Key ─────────────────────────────────────────────────────────
// Named after US-layout positions — scancodes are layout-invariant,
// so the top-left-of-home-row key reports Key::A on AZERTY too.
//
// Conventions:
//   - Unknown = 0 so zero-initialized Key values are safe.
//   - Count   = last, used to size backing arrays via static_cast<size_t>.
enum class Key : uint16_t {
    Unknown = 0,

    // Letters (A–Z)
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // Top-row digits (0–9)
    Num0, Num1, Num2, Num3, Num4,
    Num5, Num6, Num7, Num8, Num9,

    // Function keys
    F1,  F2,  F3,  F4,  F5,  F6,
    F7,  F8,  F9,  F10, F11, F12,

    // Arrow cluster
    Up, Down, Left, Right,

    // Modifier keys (left/right variants are distinct for rebinding)
    LShift, RShift,
    LCtrl,  RCtrl,
    LAlt,   RAlt,

    // Common specials
    Space, Enter, Escape, Tab, Backspace, CapsLock,

    // Edit cluster
    Insert, Delete, Home, End, PageUp, PageDown,
    PrintScreen, ScrollLock, Pause,

    // Punctuation (US-layout names — the scancode position is layout-invariant,
    // so on AZERTY the "A" key still reports Key::A)
    Comma, Period, Semicolon, Apostrophe,
    Slash, Backslash, Minus,  Equals,
    LeftBracket, RightBracket, Grave,

    Count
};

// ── MouseButton ─────────────────────────────────────────────────
// X1 and X2 are the two side buttons common on gaming mice.
enum class MouseButton : uint8_t {
    Left = 0,
    Right,
    Middle,
    X1,
    X2,

    Count
};

// ── Lifecycle ───────────────────────────────────────────────────
// Called by the engine, not by game code.
namespace input {

bool init();
void shutdown();

// Snapshots current → previous state. Call once per frame, before
// the event pump runs (i.e. before window.poll_events()).
void begin_frame();

// ── Keyboard queries ────────────────────────────────────────────
// State:  is the key held right now?
bool is_key_down(Key k);

// Edge:   did the key transition up → down this frame?
bool was_key_pressed(Key k);

// Edge:   did the key transition down → up this frame?
bool was_key_released(Key k);

// ── Mouse queries ───────────────────────────────────────────────
// Cursor position in window pixels, top-left origin. Latest value
// from the most recent MOUSE_MOTION event — no edge semantics.
Vec2 mouse_position();

// Cumulative cursor motion since the last begin_frame(). Sourced
// from SDL's per-event xrel/yrel, so it remains correct when the
// cursor is clamped to the screen edge or in relative mode.
Vec2 mouse_delta();

// Same three-flavor query set as keys, but for mouse buttons.
bool is_mouse_button_down(MouseButton b);
bool was_mouse_button_pressed(MouseButton b);
bool was_mouse_button_released(MouseButton b);

}  // namespace input

}  // namespace kuma
