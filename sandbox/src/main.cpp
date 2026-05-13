#include <kuma/kuma.h>
#include <kuma/log.h>

#include <imgui.h>

namespace {

// Empty marker component: "this entity should be drawn this frame."
// ECS handles zero-sized components without wasting space.
struct RenderTag {};

// Spin every transform around the +Y axis. Plain free function over
// a single-component view - this is what an ECS "system" looks like.
void spin_system(kuma::Registry& registry, float angle_radians) {
    for (auto [e, transform] : registry.view<kuma::Transform>()) {
        (void)e;
        transform.set_rotation_euler(angle_radians, 0.0f, 0.0f);
    }
}

// One draw call per renderable entity. The renderer doesn't know about
// ECS - this system bridges the two by feeding model matrices in a loop.
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

    // Load the glTF icosahedron alongside the engine's default quad
    // mesh. We render it once per frame in front of the ECS quad
    // grid using the renderer's debug-normal pipeline, which paints
    // each fragment by its vertex normal as RGB. Visual proof that
    // the Rust glTF baker -> binary parser -> engine load -> Vulkan
    // upload chain works end to end.
    const auto* icosahedron = kuma::get_resource_manager().load_mesh_binary(
        kuma::platform::exe_relative("assets/models/icosahedron.kmesh").c_str());
    if (!icosahedron) {
        kuma::log::warn("Icosahedron asset missing; sandbox will run without the glTF demo");
    }

    kuma::Transform icosahedron_transform;
    icosahedron_transform.set_position({0.0f, 0.0f, 12.0f});  // sit in front of the grid

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
            t.set_position({(x - kGridSize / 2) * kSpacing, (y - kGridSize / 2) * kSpacing, 0.0f});
            registry.add<kuma::Transform>(e, t);
            registry.add<RenderTag>(e, RenderTag{});
        }
    }

    kuma::log::info(
        "Sandbox ready: %d ECS entities + glTF icosahedron. Esc to quit, WASD to move, "
        "Q/E up/down, hold RMB to look.",
        kGridSize * kGridSize);

    while (kuma::begin_frame()) {
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

        // Debug overlay - F3 toggles visibility (handled inside debug
        // module). draw_default_panel is a no-op when hidden.
        kuma::debug::draw_default_panel();

        // Custom debug panel: game-specific stats. ImGui calls
        // direct from user code - no engine wrapper, this is the
        // immediate-mode philosophy. Skip the work entirely when
        // the overlay is hidden.
        if (kuma::debug::is_visible()) {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 16.0f, vp->WorkPos.y + 16.0f),
                                    ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Sandbox", nullptr, ImGuiWindowFlags_NoCollapse)) {
                kuma::debug::section_header("Camera");
                const kuma::Vec3 p = camera.position();
                ImGui::Text("Pos:   (%.1f, %.1f, %.1f)", p.x, p.y, p.z);
                ImGui::Text("Yaw:   %.2f rad", camera.yaw());
                ImGui::Text("Pitch: %.2f rad", camera.pitch());

                kuma::debug::section_header("ECS");
                ImGui::Text("Entities:   %zu", registry.view<kuma::Transform>().size());
                ImGui::Text("Renderable: %zu",
                            (registry.view<kuma::Transform, RenderTag>().size()));
            }
            ImGui::End();
        }

        // Render every entity tagged for drawing using the textured
        // pipeline (engine default).
        render_system(registry, kuma::get_renderer());

        // Then draw the glTF icosahedron with the debug-normal
        // pipeline, which paints each fragment by its normal.
        // Demonstrates: glTF -> .kmesh -> engine load -> custom
        // pipeline render. After this draw, the next frame's
        // render_system loop re-binds the quad mesh, but we also
        // need to switch the pipeline back to textured.
        if (icosahedron) {
            icosahedron_transform.set_rotation_euler(kuma::time::total() * 0.5f,
                                                      kuma::time::total() * 0.3f, 0.0f);
            kuma::get_renderer().set_mesh(icosahedron);
            kuma::get_renderer().set_model_matrix(icosahedron_transform.model_matrix());
            kuma::get_renderer().set_pipeline(1);  // debug normal
            kuma::get_renderer().draw();

            // Restore quad mesh + textured pipeline so the next
            // frame's render_system starts in the expected state.
            // (The renderer's set_mesh state persists across frames.)
            kuma::get_renderer().set_mesh(reinterpret_cast<const void*>(
                kuma::get_resource_manager().load_mesh_binary(
                    kuma::platform::exe_relative("assets/models/quad.kmesh").c_str())));
            kuma::get_renderer().set_pipeline(0);
        }

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
