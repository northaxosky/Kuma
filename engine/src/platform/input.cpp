// ── Input Implementation ────────────────────────────────────────
// Backed by the InputState struct from input_internal.h. The public
// kuma::input:: namespace functions are thin forwarders over a
// file-local static state instance.
//
// The one piece of real logic here is the SDL → Key translation at
// the top of the file. Everything else is glue.

#include "platform/input_internal.h"

#include <kuma/log.h>

namespace kuma {

namespace {

// ── SDL Translation ─────────────────────────────────────────────
// Maps SDL scancodes to our SDL-free Key enum. A switch compiles
// to a jump table on all major compilers, so this is as fast as a
// precomputed array lookup and far more readable.
//
// Unmapped scancodes (media keys, keypad keys we haven't added yet,
// etc.) land on Key::Unknown and are silently ignored by callers.
Key sdl_scancode_to_key(SDL_Scancode sc) {
    switch (sc) {
        // Letters
        case SDL_SCANCODE_A: return Key::A;
        case SDL_SCANCODE_B: return Key::B;
        case SDL_SCANCODE_C: return Key::C;
        case SDL_SCANCODE_D: return Key::D;
        case SDL_SCANCODE_E: return Key::E;
        case SDL_SCANCODE_F: return Key::F;
        case SDL_SCANCODE_G: return Key::G;
        case SDL_SCANCODE_H: return Key::H;
        case SDL_SCANCODE_I: return Key::I;
        case SDL_SCANCODE_J: return Key::J;
        case SDL_SCANCODE_K: return Key::K;
        case SDL_SCANCODE_L: return Key::L;
        case SDL_SCANCODE_M: return Key::M;
        case SDL_SCANCODE_N: return Key::N;
        case SDL_SCANCODE_O: return Key::O;
        case SDL_SCANCODE_P: return Key::P;
        case SDL_SCANCODE_Q: return Key::Q;
        case SDL_SCANCODE_R: return Key::R;
        case SDL_SCANCODE_S: return Key::S;
        case SDL_SCANCODE_T: return Key::T;
        case SDL_SCANCODE_U: return Key::U;
        case SDL_SCANCODE_V: return Key::V;
        case SDL_SCANCODE_W: return Key::W;
        case SDL_SCANCODE_X: return Key::X;
        case SDL_SCANCODE_Y: return Key::Y;
        case SDL_SCANCODE_Z: return Key::Z;

        // Top-row digits
        case SDL_SCANCODE_0: return Key::Num0;
        case SDL_SCANCODE_1: return Key::Num1;
        case SDL_SCANCODE_2: return Key::Num2;
        case SDL_SCANCODE_3: return Key::Num3;
        case SDL_SCANCODE_4: return Key::Num4;
        case SDL_SCANCODE_5: return Key::Num5;
        case SDL_SCANCODE_6: return Key::Num6;
        case SDL_SCANCODE_7: return Key::Num7;
        case SDL_SCANCODE_8: return Key::Num8;
        case SDL_SCANCODE_9: return Key::Num9;

        // Function keys
        case SDL_SCANCODE_F1:  return Key::F1;
        case SDL_SCANCODE_F2:  return Key::F2;
        case SDL_SCANCODE_F3:  return Key::F3;
        case SDL_SCANCODE_F4:  return Key::F4;
        case SDL_SCANCODE_F5:  return Key::F5;
        case SDL_SCANCODE_F6:  return Key::F6;
        case SDL_SCANCODE_F7:  return Key::F7;
        case SDL_SCANCODE_F8:  return Key::F8;
        case SDL_SCANCODE_F9:  return Key::F9;
        case SDL_SCANCODE_F10: return Key::F10;
        case SDL_SCANCODE_F11: return Key::F11;
        case SDL_SCANCODE_F12: return Key::F12;

        // Arrows
        case SDL_SCANCODE_UP:    return Key::Up;
        case SDL_SCANCODE_DOWN:  return Key::Down;
        case SDL_SCANCODE_LEFT:  return Key::Left;
        case SDL_SCANCODE_RIGHT: return Key::Right;

        // Modifiers (left/right kept distinct)
        case SDL_SCANCODE_LSHIFT: return Key::LShift;
        case SDL_SCANCODE_RSHIFT: return Key::RShift;
        case SDL_SCANCODE_LCTRL:  return Key::LCtrl;
        case SDL_SCANCODE_RCTRL:  return Key::RCtrl;
        case SDL_SCANCODE_LALT:   return Key::LAlt;
        case SDL_SCANCODE_RALT:   return Key::RAlt;

        // Common specials (SDL calls Enter "RETURN")
        case SDL_SCANCODE_SPACE:     return Key::Space;
        case SDL_SCANCODE_RETURN:    return Key::Enter;
        case SDL_SCANCODE_ESCAPE:    return Key::Escape;
        case SDL_SCANCODE_TAB:       return Key::Tab;
        case SDL_SCANCODE_BACKSPACE: return Key::Backspace;
        case SDL_SCANCODE_CAPSLOCK:  return Key::CapsLock;

        // Edit cluster
        case SDL_SCANCODE_INSERT:      return Key::Insert;
        case SDL_SCANCODE_DELETE:      return Key::Delete;
        case SDL_SCANCODE_HOME:        return Key::Home;
        case SDL_SCANCODE_END:         return Key::End;
        case SDL_SCANCODE_PAGEUP:      return Key::PageUp;
        case SDL_SCANCODE_PAGEDOWN:    return Key::PageDown;
        case SDL_SCANCODE_PRINTSCREEN: return Key::PrintScreen;
        case SDL_SCANCODE_SCROLLLOCK:  return Key::ScrollLock;
        case SDL_SCANCODE_PAUSE:       return Key::Pause;

        // Punctuation
        case SDL_SCANCODE_COMMA:        return Key::Comma;
        case SDL_SCANCODE_PERIOD:       return Key::Period;
        case SDL_SCANCODE_SEMICOLON:    return Key::Semicolon;
        case SDL_SCANCODE_APOSTROPHE:   return Key::Apostrophe;
        case SDL_SCANCODE_SLASH:        return Key::Slash;
        case SDL_SCANCODE_BACKSLASH:    return Key::Backslash;
        case SDL_SCANCODE_MINUS:        return Key::Minus;
        case SDL_SCANCODE_EQUALS:       return Key::Equals;
        case SDL_SCANCODE_LEFTBRACKET:  return Key::LeftBracket;
        case SDL_SCANCODE_RIGHTBRACKET: return Key::RightBracket;
        case SDL_SCANCODE_GRAVE:        return Key::Grave;

        default: return Key::Unknown;
    }
}

MouseButton sdl_button_to_mouse_button(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT:   return MouseButton::Left;
        case SDL_BUTTON_RIGHT:  return MouseButton::Right;
        case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
        case SDL_BUTTON_X1:     return MouseButton::X1;
        case SDL_BUTTON_X2:     return MouseButton::X2;
        default:                return MouseButton::Count;
    }
}

// ── Global State ────────────────────────────────────────────────
// File-local so nothing outside this translation unit can poke at
// it. The kuma::input:: namespace functions below are its only
// entry points.
InputState s_state;

}  // namespace

// ── InputState::process_event ───────────────────────────────────
// The one event handler. Translates SDL's event-driven view into
// per-key boolean state. Called from input::process_sdl_event below.
void InputState::process_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN: {
            // SDL sends repeated KEY_DOWN while a key is held (~30Hz
            // after an initial delay). Our polled model already handles
            // this correctly via the edge compare, but skipping repeats
            // here avoids redundant work and keeps intent clear.
            if (e.key.repeat) {
                break;
            }
            const Key k = sdl_scancode_to_key(e.key.scancode);
            current_keys[static_cast<std::size_t>(k)] = true;
            break;
        }
        case SDL_EVENT_KEY_UP: {
            const Key k = sdl_scancode_to_key(e.key.scancode);
            current_keys[static_cast<std::size_t>(k)] = false;
            break;
        }

        // ── Mouse ───────────────────────────────────────────────
        case SDL_EVENT_MOUSE_MOTION: {
            // Latest-wins for absolute position.
            mouse_pos = {e.motion.x, e.motion.y};
            // Accumulate relative deltas; multiple motion events can
            // fire per frame at high polling rates and they must all
            // count toward this frame's delta.
            mouse_delta_accum += Vec2(e.motion.xrel, e.motion.yrel);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            // SDL orders buttons Left, Middle, Right, X1, X2; Kuma orders
            // Left, Right, Middle, X1, X2. Use an explicit mapping so RMB
            // never accidentally becomes MMB.
            const MouseButton button = sdl_button_to_mouse_button(e.button.button);
            if (button == MouseButton::Count) {
                break;
            }
            const std::size_t idx = static_cast<std::size_t>(button);
            current_mouse_buttons[idx] = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            break;
        }

        // Focus-loss handling arrives in a later commit.
        default:
            break;
    }
}

// ── Public API Forwarders ───────────────────────────────────────
namespace input {

bool init() {
    s_state.reset();
    kuma::log::info("Input initialized");
    return true;
}

void shutdown() {
    s_state.reset();
    kuma::log::info("Input shut down");
}

void begin_frame() {
    s_state.begin_frame();
}

void process_sdl_event(const SDL_Event& e) {
    s_state.process_event(e);
}

bool is_key_down(Key k)      { return s_state.is_key_down(k); }
bool was_key_pressed(Key k)  { return s_state.was_key_pressed(k); }
bool was_key_released(Key k) { return s_state.was_key_released(k); }

Vec2 mouse_position() { return s_state.mouse_position(); }
Vec2 mouse_delta()    { return s_state.mouse_delta(); }

bool is_mouse_button_down(MouseButton b)      { return s_state.is_mouse_button_down(b); }
bool was_mouse_button_pressed(MouseButton b)  { return s_state.was_mouse_button_pressed(b); }
bool was_mouse_button_released(MouseButton b) { return s_state.was_mouse_button_released(b); }

}  // namespace input

}  // namespace kuma
