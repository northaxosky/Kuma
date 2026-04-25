#include <kuma/kuma.h>
#include <kuma/log.h>

int main() {
    kuma::EngineConfig config{};
    config.app_name = "Kuma Sandbox";
    config.window_width = 1920;
    config.window_height = 1080;
    config.present_mode = kuma::PresentMode::Vsync;

    if (!kuma::init(config)) {
        return 1;
    }

    kuma::Camera camera;
    camera.set_perspective(0.78539816f,
                           static_cast<float>(config.window_width) /
                               static_cast<float>(config.window_height),
                           0.1f, 100.0f);

    kuma::FreeFlyCameraController camera_controller;

    kuma::log::info(
        "Sandbox ready. Try: Esc to quit, WASD to move camera, Q/E down/up, LMB to log mouse position.");

    while (kuma::begin_frame()) {
        // ── Phase 3: UPDATE ─────────────────────────────────────
        // Edge queries (was_pressed / was_released) for things that
        // happen ONCE per user action — logging, menu toggles, etc.
        // State queries (is_key_down) for per-frame work like camera
        // movement, handled below by FreeFlyCameraController.

        if (kuma::input::was_key_pressed(kuma::Key::Escape)) {
            kuma::log::info("Esc pressed - quitting");
            break;
        }

        camera_controller.update(camera);
        kuma::get_renderer().set_view_projection(camera.view_projection());

        if (kuma::input::was_mouse_button_pressed(kuma::MouseButton::Left)) {
            const kuma::Vec2 p = kuma::input::mouse_position();
            kuma::log::info("LMB click at (%.0f, %.0f)", p.x, p.y);
        }
        if (kuma::input::was_mouse_button_pressed(kuma::MouseButton::Right)) {
            kuma::log::info("RMB click");
        }

        // ── Time demo ───────────────────────────────────────────
        // Throttled FPS log — every 60 ticks, instantaneous dt and
        // its reciprocal. Skip frame 1 because the first tick reports
        // dt=0 by design (no previous frame to measure against), and
        // 1/0 is a noisy way to start the log.
        const uint64_t frame = kuma::time::frame_count();
        if (frame > 1 && frame % 60 == 0) {
            const float dt = kuma::time::delta();
            kuma::log::info("frame %llu  dt=%.2fms  (~%.0f FPS)  total=%.1fs",
                            static_cast<unsigned long long>(frame), dt * 1000.0f,
                            dt > 0.0f ? 1.0f / dt : 0.0f, kuma::time::total());
        }

        kuma::end_frame();
    }

    kuma::shutdown();
    return 0;
}
