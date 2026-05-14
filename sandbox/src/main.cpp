#include <kuma/kuma.h>
#include <kuma/log.h>

#include <imgui.h>

#include <vector>

namespace {

// Empty marker component: "this entity should be drawn this frame."
// ECS handles zero-sized components without wasting space.
struct RenderTag {};

// Marks an entity as a runtime-spawned physics icosahedron. R-key
// removes everything tagged with this, leaving the grid + the
// decorative spinner alone.
struct SpawnedTag {};

// Sandbox can swap between an FPS character controller and the
// existing free-fly camera for debug inspection. T toggles.
enum class CameraMode { Fps, Fly };

// Spin every grid quad around the +Y axis. Plain free function over
// a two-component view - the SpawnedTag filter skips the runtime
// icosahedrons so physics owns their rotations.
void spin_system(kuma::Registry& registry, float angle_radians) {
    for (auto [e, transform, tag] : registry.view<kuma::Transform, RenderTag>()) {
        (void)e;
        (void)tag;
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

// Spawn a Dynamic physics icosahedron a few units in front of the
// camera. Gravity does the rest. The icosahedron mesh is rendered
// over a sphere collider - close enough for the visual demo.
kuma::EntityID spawn_physics_icosahedron(kuma::Registry& registry,
                                          const kuma::Camera& camera) {
    constexpr float kSpawnDistance = 3.0f;
    constexpr float kRadius = 0.3f;

    kuma::EntityID e = registry.create_entity();

    kuma::Transform t;
    t.set_position(camera.position() + camera.forward() * kSpawnDistance);
    t.set_scale(kRadius * 2.0f);  // icosahedron mesh is unit-sized
    registry.add(e, t);

    kuma::physics::PhysicsBody body;
    body.type = kuma::physics::BodyType::Dynamic;
    body.shape = kuma::physics::BodyShape::Sphere;
    body.dimensions = {kRadius, 0.0f, 0.0f};
    body.restitution = 0.4f;
    body.friction = 0.6f;
    body.layer = kuma::physics::PhysicsLayer::Dynamic;
    registry.add(e, body);

    registry.add(e, SpawnedTag{});
    return e;
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
    // Lift the fly-mode start a bit above the floor so the player
    // can survey the scene before dropping into the FPS body.
    camera.set_position({0.0f, 4.0f, 18.0f});

    kuma::FreeFlyCameraController fly_controller;
    fly_controller.mouse_sensitivity = 0.0025f;
    kuma::FpsCameraController fps_controller;
    fps_controller.mouse_sensitivity = 0.0025f;

    CameraMode mode = CameraMode::Fps;

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

    const auto* quad_mesh = kuma::get_resource_manager().load_mesh_binary(
        kuma::platform::exe_relative("assets/models/quad.kmesh").c_str());

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

    // ── Physics scene: invisible static floor ────────────────────
    // Sits a comfortable distance below the camera so spawned bodies
    // and the player character have somewhere to land.
    constexpr float kFloorY = -8.0f;
    constexpr float kFloorHalfExtents = 30.0f;
    {
        kuma::EntityID floor = registry.create_entity();
        kuma::Transform t;
        t.set_position({0.0f, kFloorY, 0.0f});
        registry.add(floor, t);

        kuma::physics::PhysicsBody body;
        body.type = kuma::physics::BodyType::Static;
        body.shape = kuma::physics::BodyShape::Box;
        body.dimensions = {kFloorHalfExtents, 0.5f, kFloorHalfExtents};
        body.layer = kuma::physics::PhysicsLayer::StaticWorld;
        registry.add(floor, body);
    }

    // ── Player character ────────────────────────────────────────
    // Spawn just above the floor so the controller settles in one
    // frame. The Character struct's defaults (1.8m capsule, 5 m/s
    // walk, 5.5 m/s jump) match a typical FPS player.
    kuma::EntityID player = registry.create_entity();
    {
        kuma::Transform t;
        t.set_position({0.0f, kFloorY + 2.0f, 12.0f});  // in front of the grid
        registry.add(player, t);
        kuma::Character c;
        registry.add(player, c);
    }

    constexpr uint32_t kMaxSpawned = 200;
    std::vector<kuma::EntityID> spawned;

    kuma::log::info(
        "Sandbox ready: %d quads + glTF spinner + physics floor at y=%.1f. "
        "WASD walk, Space jump, mouse look. T to toggle Fly mode (debug). "
        "F spawns icosahedron, R clears them.",
        kGridSize * kGridSize, kFloorY);

    // FPS mode wants the cursor captured for relative mouse motion.
    // Fly mode keeps the existing RMB-hold-to-look behavior.
    kuma::get_window().set_relative_mouse_mode(true);

    while (kuma::begin_frame()) {
        if (kuma::input::was_key_pressed(kuma::Key::Escape)) {
            kuma::get_window().set_relative_mouse_mode(false);
            kuma::log::info("Esc pressed - quitting");
            break;
        }

        if (kuma::input::was_key_pressed(kuma::Key::T)) {
            mode = (mode == CameraMode::Fps) ? CameraMode::Fly : CameraMode::Fps;
            const bool fps_now = (mode == CameraMode::Fps);
            kuma::get_window().set_relative_mouse_mode(fps_now);
            kuma::log::info("Camera mode: %s", fps_now ? "FPS" : "Fly");
        }

        if (mode == CameraMode::Fly) {
            // Fly-mode RMB-hold matches the original sandbox feel.
            if (kuma::input::was_mouse_button_pressed(kuma::MouseButton::Right)) {
                kuma::get_window().set_relative_mouse_mode(true);
            }
            if (kuma::input::was_mouse_button_released(kuma::MouseButton::Right)) {
                kuma::get_window().set_relative_mouse_mode(false);
            }
            fly_controller.update(camera);
        } else {
            kuma::Character& pc = registry.get<kuma::Character>(player);
            kuma::Transform& pt = registry.get<kuma::Transform>(player);
            fps_controller.update(pc, pt, camera);
        }

        kuma::get_renderer().set_view_projection(camera.view_projection());

        // F spawns an icosahedron in front of the camera. Spawn is
        // capped so the body pool can't exhaust accidentally.
        if (kuma::input::was_key_pressed(kuma::Key::F) && spawned.size() < kMaxSpawned) {
            spawned.push_back(spawn_physics_icosahedron(registry, camera));
        }

        // R wipes every spawned body. destroy_entity routes through
        // physics so the Jolt-side body is freed alongside the ECS slot.
        if (kuma::input::was_key_pressed(kuma::Key::R) && !spawned.empty()) {
            for (kuma::EntityID e : spawned) {
                kuma::physics::destroy_entity(registry, e);
            }
            spawned.clear();
            kuma::log::info("Spawned bodies cleared");
        }

        // Step character first so its motion + pushes feed into the
        // physics step. Then physics: integrate forces, resolve
        // collisions, sync dynamic body poses back into Transforms.
        const float dt = kuma::time::delta();
        kuma::character::simulate(dt, registry);
        kuma::physics::simulate(dt, registry);

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
                ImGui::Text("Mode:  %s", mode == CameraMode::Fps ? "FPS" : "Fly");
                ImGui::Text("Pos:   (%.1f, %.1f, %.1f)", p.x, p.y, p.z);
                ImGui::Text("Yaw:   %.2f rad", camera.yaw());
                ImGui::Text("Pitch: %.2f rad", camera.pitch());

                kuma::debug::section_header("Character");
                const auto& pc = registry.get<kuma::Character>(player);
                const auto& pt = registry.get<kuma::Transform>(player);
                ImGui::Text("Pos:      (%.1f, %.1f, %.1f)",
                            pt.position().x, pt.position().y, pt.position().z);
                ImGui::Text("Velocity: (%.1f, %.1f, %.1f)",
                            pc.velocity.x, pc.velocity.y, pc.velocity.z);
                if (pc.on_ground) {
                    ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "On ground: yes");
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.4f, 1.0f), "On ground: no");
                }
                if (pc.on_steep) {
                    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.4f, 1.0f), "On steep slope");
                }

                kuma::debug::section_header("ECS");
                ImGui::Text("Entities:   %zu", registry.view<kuma::Transform>().size());
                ImGui::Text("Renderable: %zu",
                            (registry.view<kuma::Transform, RenderTag>().size()));

                kuma::debug::section_header("Physics");
                ImGui::Text("Bodies:    %u", kuma::physics::body_count());
                ImGui::Text("Spawned:   %zu / %u", spawned.size(), kMaxSpawned);
                ImGui::TextDisabled("F to spawn, R to reset, T to toggle Fly");
            }
            ImGui::End();
        }

        // Render every entity tagged for drawing using the textured
        // pipeline (engine default).
        render_system(registry, kuma::get_renderer());

        // Draw the icosahedron mesh in two flavors: the decorative
        // spinner at the original anchor, then every spawned physics
        // icosahedron. Both share the debug-normal pipeline so the
        // mesh + pipeline bind happens once.
        if (icosahedron) {
            kuma::get_renderer().set_mesh(icosahedron);
            kuma::get_renderer().set_pipeline(1);  // debug normal

            icosahedron_transform.set_rotation_euler(kuma::time::total() * 0.5f,
                                                      kuma::time::total() * 0.3f, 0.0f);
            kuma::get_renderer().set_model_matrix(icosahedron_transform.model_matrix());
            kuma::get_renderer().draw();

            for (auto [e, transform, tag] : registry.view<kuma::Transform, SpawnedTag>()) {
                (void)e;
                (void)tag;
                kuma::get_renderer().set_model_matrix(transform.model_matrix());
                kuma::get_renderer().draw();
            }

            // Restore quad mesh + textured pipeline so the next
            // frame's render_system starts in the expected state.
            if (quad_mesh) {
                kuma::get_renderer().set_mesh(quad_mesh);
            }
            kuma::get_renderer().set_pipeline(0);
        }

        // Throttled FPS log (every 60 ticks, skipping frame 1 where
        // dt=0 by design).
        const uint64_t frame = kuma::time::frame_count();
        if (frame > 1 && frame % 60 == 0) {
            kuma::log::info("frame %llu  dt=%.2fms  (~%.0f FPS)  total=%.1fs",
                            static_cast<unsigned long long>(frame), dt * 1000.0f,
                            dt > 0.0f ? 1.0f / dt : 0.0f, kuma::time::total());
        }

        kuma::end_frame();
    }

    kuma::shutdown();
    return 0;
}
