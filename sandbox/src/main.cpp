#include <kuma/kuma.h>
#include <kuma/log.h>

namespace {

// Marker component: "this entity should be drawn this frame."
// Empty struct - it's pure presence/absence info for the render
// system's view filter. ECS handles empty components without
// wasting space.
struct RenderTag {};

// SYSTEM (Phase 3 UPDATE): spin every transform around +Y.
// One free function per system, called from the main loop. No
// scheduler in v1 - ordering is hardcoded in main().
void spin_system(kuma::Registry& registry, float angle_radians) {
    for (auto [e, transform] : registry.view<kuma::Transform>()) {
        (void)e;
        transform.set_rotation_euler(angle_radians, 0.0f, 0.0f);
    }
}

// SYSTEM (Phase 4 RENDER): one draw call per renderable entity.
// Renderer doesn't know about ECS - it just gets fed model
// matrices and draw() calls in a loop.
void render_system(kuma::Registry& registry, kuma::Renderer& renderer) {
    for (auto [e, transform, tag] : registry.view<kuma::Transform, RenderTag>()) {
        (void)e;
        (void)tag;
        renderer.set_model_matrix(transform.model_matrix());
        renderer.draw();
    }
}

}  // namespace

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
    // Pull the camera back so the whole 10x10 grid is in frame.
    camera.set_position({0.0f, 0.0f, 18.0f});

    kuma::FreeFlyCameraController camera_controller;
    camera_controller.mouse_sensitivity = 0.0025f;

    // ── Build the ECS world: 10x10 grid of spinning quads ───────
    // Every entity has Transform (its position/rotation/scale) and
    // RenderTag (marks it as drawable). The grid offsets lift the
    // quads off the world origin so the camera can see them all.
    kuma::Registry registry;
    constexpr int kGridSize = 10;
    constexpr float kSpacing = 1.5f;
    for (int x = 0; x < kGridSize; x++) {
        for (int y = 0; y < kGridSize; y++) {
            kuma::EntityID e = registry.create_entity();

            kuma::Transform t;
            t.set_position({(x - kGridSize / 2) * kSpacing,
                            (y - kGridSize / 2) * kSpacing, 0.0f});
            registry.add<kuma::Transform>(e, t);
            registry.add<RenderTag>(e, RenderTag{});
        }
    }

    kuma::log::info(
        "Sandbox ready: %d ECS entities. Esc to quit, WASD to move, Q/E up/down, hold RMB to look.",
        kGridSize * kGridSize);

    while (kuma::begin_frame()) {
        // ── Phase 3: UPDATE ─────────────────────────────────────
        if (kuma::input::was_key_pressed(kuma::Key::Escape)) {
            kuma::get_window().set_relative_mouse_mode(false);
            kuma::log::info("Esc pressed - quitting");
            break;
        }

        if (kuma::input::was_mouse_button_pressed(kuma::MouseButton::Right)) {
            kuma::get_window().set_relative_mouse_mode(true);
        }
        if (kuma::input::was_mouse_button_released(kuma::MouseButton::Right)) {
            kuma::get_window().set_relative_mouse_mode(false);
        }

        camera_controller.update(camera);
        kuma::get_renderer().set_view_projection(camera.view_projection());

        // Run gameplay systems in declared order.
        spin_system(registry, kuma::time::total());

        // ── Phase 4 driver: render system feeds the renderer ────
        render_system(registry, kuma::get_renderer());

        if (kuma::input::was_mouse_button_pressed(kuma::MouseButton::Left)) {
            const kuma::Vec2 p = kuma::input::mouse_position();
            kuma::log::info("LMB click at (%.0f, %.0f)", p.x, p.y);
        }

        // Throttled FPS log (every 60 ticks, skipping frame 1 where
        // dt=0 by design).
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
