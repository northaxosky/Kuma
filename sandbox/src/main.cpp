#include <kuma/kuma.h>
#include <kuma/log.h>

int main() {
    kuma::EngineConfig config{};
    config.app_name = "Kuma Sandbox";
    config.window_width = 1920;
    config.window_height = 1080;

    if (!kuma::init(config)) {
        return 1;
    }

    kuma::log::info("Sandbox ready. Try: Esc to quit, WASD to log key press, LMB to log mouse position.");

    while (kuma::begin_frame()) {
        // ── Phase 3: UPDATE ─────────────────────────────────────
        // Edge queries (was_pressed / was_released) for things that
        // happen ONCE per user action — logging, menu toggles, etc.
        // State queries (is_key_down) for per-frame work like moving
        // a camera, which we'll wire up in a later module.

        if (kuma::input::was_key_pressed(kuma::Key::Escape)) {
            kuma::log::info("Esc pressed — quitting");
            break;
        }

        if (kuma::input::was_key_pressed(kuma::Key::W)) kuma::log::info("Pressed: W");
        if (kuma::input::was_key_pressed(kuma::Key::A)) kuma::log::info("Pressed: A");
        if (kuma::input::was_key_pressed(kuma::Key::S)) kuma::log::info("Pressed: S");
        if (kuma::input::was_key_pressed(kuma::Key::D)) kuma::log::info("Pressed: D");

        if (kuma::input::was_mouse_button_pressed(kuma::MouseButton::Left)) {
            const kuma::Vec2 p = kuma::input::mouse_position();
            kuma::log::info("LMB click at (%.0f, %.0f)", p.x, p.y);
        }
        if (kuma::input::was_mouse_button_pressed(kuma::MouseButton::Right)) {
            kuma::log::info("RMB click");
        }

        kuma::end_frame();
    }

    kuma::shutdown();
    return 0;
}
