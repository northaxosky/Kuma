#pragma once

// ── Input Internal ──────────────────────────────────────────────
// Non-public header for the input system. Lives in src/ (not in
// include/kuma/) so it's reachable from other engine .cpp files
// and tests, but game code never sees it.
//
// This is the seam where SDL meets kuma. The public kuma/input.h
// is SDL-free by design; SDL types only appear below.

#include <kuma/input.h>

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstring>

namespace kuma {

// ── InputState ──────────────────────────────────────────────────
// The complete input snapshot. All state the input system tracks,
// driven by process_event() and queried via the is_*/was_* members.
//
// Separate from the public `kuma::input::` namespace so that tests
// can construct their own InputState with zero globals, no SDL init,
// no engine lifecycle — just pump fake events and assert.
struct InputState {
    // Backing arrays sized by the Key enum's Count sentinel — grows
    // automatically when keys are added to the enum.
    static constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);
    static constexpr std::size_t kMouseButtonCount =
        static_cast<std::size_t>(MouseButton::Count);

    bool current_keys[kKeyCount]  = {};
    bool previous_keys[kKeyCount] = {};

    // ── Mouse ───────────────────────────────────────────────────
    // Position: latest-wins (set from every MOUSE_MOTION event).
    Vec2 mouse_pos = {};

    // Delta: accumulated within a frame, reset in begin_frame().
    // Sourced from e.motion.xrel/yrel so it survives edge clamping.
    Vec2 mouse_delta_accum = {};

    // Same edge-detection pair as keys, but per button.
    bool current_mouse_buttons[kMouseButtonCount]  = {};
    bool previous_mouse_buttons[kMouseButtonCount] = {};

    // Zero all state. Called on startup and shutdown.
    void reset() {
        std::memset(current_keys, 0, sizeof(current_keys));
        std::memset(previous_keys, 0, sizeof(previous_keys));
        mouse_pos = {};
        mouse_delta_accum = {};
        std::memset(current_mouse_buttons, 0, sizeof(current_mouse_buttons));
        std::memset(previous_mouse_buttons, 0, sizeof(previous_mouse_buttons));
    }

    // Snapshot the current frame's state into "previous" so edge
    // detection (current && !previous) works for the next frame.
    // Also resets the accumulated mouse delta — new frame, new delta.
    // Call this BEFORE draining SDL events into `current`.
    void begin_frame() {
        std::memcpy(previous_keys, current_keys, sizeof(current_keys));
        std::memcpy(previous_mouse_buttons, current_mouse_buttons,
                    sizeof(current_mouse_buttons));
        mouse_delta_accum = {};
    }

    // Fold a single SDL event into the current state.
    void process_event(const SDL_Event& e);

    // ── Keyboard queries ────────────────────────────────────────
    bool is_key_down(Key k) const {
        return current_keys[static_cast<std::size_t>(k)];
    }
    bool was_key_pressed(Key k) const {
        const std::size_t i = static_cast<std::size_t>(k);
        return current_keys[i] && !previous_keys[i];
    }
    bool was_key_released(Key k) const {
        const std::size_t i = static_cast<std::size_t>(k);
        return !current_keys[i] && previous_keys[i];
    }

    // ── Mouse queries ───────────────────────────────────────────
    Vec2 mouse_position() const { return mouse_pos; }
    Vec2 mouse_delta() const { return mouse_delta_accum; }

    bool is_mouse_button_down(MouseButton b) const {
        return current_mouse_buttons[static_cast<std::size_t>(b)];
    }
    bool was_mouse_button_pressed(MouseButton b) const {
        const std::size_t i = static_cast<std::size_t>(b);
        return current_mouse_buttons[i] && !previous_mouse_buttons[i];
    }
    bool was_mouse_button_released(MouseButton b) const {
        const std::size_t i = static_cast<std::size_t>(b);
        return !current_mouse_buttons[i] && previous_mouse_buttons[i];
    }
};

namespace input {

// ── SDL Event Forwarding ────────────────────────────────────────
// Called by platform/window.cpp after SDL_PollEvent. Not part of
// the public API because it takes an SDL type.
void process_sdl_event(const SDL_Event& e);

}  // namespace input

}  // namespace kuma
