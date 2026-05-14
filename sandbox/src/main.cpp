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
                                          const kuma::Camera& camera,
                                          const kuma::audio::Sound* thud) {
    constexpr float kSpawnDistance = 3.0f;
    constexpr float kRadius = 0.3f;

    kuma::EntityID e = registry.create_entity();

    const kuma::Vec3 spawn_pos = camera.position() + camera.forward() * kSpawnDistance;
    kuma::Transform t;
    t.set_position(spawn_pos);
    t.set_scale(kRadius * 2.0f);
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

    // Punchy spawn cue - the thud plays from the spawn position so
    // it fades correctly with distance and pans based on the listener.
    if (thud != nullptr) {
        kuma::audio::play_sound_at(thud, spawn_pos);
    }
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
    // Lift fly-mode start above Sponza's hall so the player can
    // survey the space before dropping into the FPS body.
    camera.set_position({-13.0f, 8.0f, 0.0f});

    kuma::FreeFlyCameraController fly_controller;
    fly_controller.mouse_sensitivity = 0.0025f;
    kuma::FpsCameraController fps_controller;
    fps_controller.mouse_sensitivity = 0.0025f;

    CameraMode mode = CameraMode::Fps;

    // Load the glTF icosahedron - rendered with the debug-normal
    // pipeline for spawned physics shots so they stand out from
    // Sponza's textured geometry.
    const auto* icosahedron = kuma::get_resource_manager().load_mesh_binary(
        kuma::platform::exe_relative("assets/models/icosahedron.kmesh").c_str());
    if (!icosahedron) {
        kuma::log::warn("Icosahedron asset missing; sandbox will run without the spawn demo");
    }

    const auto* quad_mesh = kuma::get_resource_manager().load_mesh_binary(
        kuma::platform::exe_relative("assets/models/quad.kmesh").c_str());
    (void)quad_mesh;

    kuma::Registry registry;

    // ── Sponza scene ─────────────────────────────────────────────
    // Replaces the previous 100-quad grid + decorative icosahedron.
    // load_and_spawn returns a SceneInstance with the count of
    // entities spawned + any failed mesh loads so the debug overlay
    // can surface partial-load issues.
    auto sponza_instance = kuma::scene::load_and_spawn(
        kuma::platform::exe_relative("assets/scenes/Sponza.kscene").c_str(),
        registry);
    if (!sponza_instance.valid()) {
        kuma::log::warn("Sponza scene failed to load; sandbox will run with no level");
    }

    // ── Physics scene: invisible static floor ────────────────────
    // Sponza's floor sits at y = 0 in source. Position our physics
    // floor box so its TOP surface lines up with y = 0 (half-extent
    // 0.5 -> center at y = -0.5).
    constexpr float kFloorY = -0.5f;
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
    // Spawn at one end of Sponza's main hall (which runs along X),
    // looking down its length. Sponza's coord system has the floor
    // at y=0 so a 2m starting height puts the character just above
    // the floor with the camera at ~3.6m (eye_height + character).
    kuma::EntityID player = registry.create_entity();
    {
        kuma::Transform t;
        t.set_position({-13.0f, 2.0f, 0.0f});
        registry.add(player, t);
        kuma::Character c;
        c.yaw_radians = -1.57079633f;  // face +X (down the hall)
        registry.add(player, c);
    }

    // ── Audio assets ────────────────────────────────────────────
    const auto* thud_sound = kuma::audio::load_sound(
        kuma::platform::exe_relative("assets/sounds/thud.ksound").c_str());
    const auto* ambient_sound = kuma::audio::load_sound(
        kuma::platform::exe_relative("assets/sounds/ambient.ksound").c_str());

    if (ambient_sound != nullptr) {
        kuma::EntityID music = registry.create_entity();
        kuma::AudioSource src;
        src.sound = ambient_sound;
        src.spatial = false;
        src.looping = true;
        src.volume = 0.4f;
        src.play_on_create = true;
        registry.add(music, src);
    }

    constexpr uint32_t kMaxSpawned = 200;
    std::vector<kuma::EntityID> spawned;

    kuma::log::info(
        "Sandbox ready: Sponza scene (%u entities, %u meshes failed) + physics floor + "
        "ambient music. WASD walk, Space jump, mouse look. T toggle Fly. F spawn, R clear.",
        sponza_instance.entity_count, sponza_instance.mesh_failed_count);

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

        if (kuma::input::was_key_pressed(kuma::Key::F) && spawned.size() < kMaxSpawned) {
            spawned.push_back(spawn_physics_icosahedron(registry, camera, thud_sound));
        }

        if (kuma::input::was_key_pressed(kuma::Key::R) && !spawned.empty()) {
            for (kuma::EntityID e : spawned) {
                kuma::physics::destroy_entity(registry, e);
            }
            spawned.clear();
            kuma::log::info("Spawned bodies cleared");
        }

        const float dt = kuma::time::delta();
        kuma::character::simulate(dt, registry);
        kuma::physics::simulate(dt, registry);

        kuma::audio::set_listener_pose(camera.position(),
                                        camera.forward(),
                                        {0.0f, 1.0f, 0.0f});
        kuma::audio::simulate(registry);

        kuma::debug::draw_default_panel();

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

                kuma::debug::section_header("Scene");
                ImGui::Text("Sponza entities: %u", sponza_instance.entity_count);
                if (sponza_instance.mesh_failed_count > 0) {
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.4f, 1.0f),
                                       "Failed meshes: %u", sponza_instance.mesh_failed_count);
                }

                kuma::debug::section_header("Physics");
                ImGui::Text("Bodies:    %u", kuma::physics::body_count());
                ImGui::Text("Spawned:   %zu / %u", spawned.size(), kMaxSpawned);
                ImGui::TextDisabled("F to spawn, R to reset, T to toggle Fly");

                kuma::debug::section_header("Audio");
                ImGui::Text("Playing:   %u", kuma::audio::playing_count());
            }
            ImGui::End();
        }

        // ── Render Sponza meshes (textured pipeline) ─────────────
        // Every entity with a MeshRef goes through the default
        // pipeline. Sponza meshes will render with whatever
        // placeholder texture is bound - they look "right" in
        // structure (geometry + UVs) but will get the wrong colors
        // until the Materials module lands.
        kuma::get_renderer().set_pipeline(0);
        for (auto [e, transform, mesh_ref] : registry.view<kuma::Transform, kuma::MeshRef>()) {
            (void)e;
            if (mesh_ref.mesh == nullptr) continue;
            kuma::get_renderer().set_mesh(mesh_ref.mesh);
            kuma::get_renderer().set_model_matrix(transform.model_matrix());
            kuma::get_renderer().draw();
        }

        // ── Render physics icosahedrons (debug-normal pipeline) ──
        // Each F-spawned body shares the icosahedron mesh and the
        // rainbow-normal shader so they pop visually against
        // Sponza's monochrome geometry.
        if (icosahedron) {
            kuma::get_renderer().set_mesh(icosahedron);
            kuma::get_renderer().set_pipeline(1);

            for (auto [e, transform, tag] : registry.view<kuma::Transform, SpawnedTag>()) {
                (void)e;
                (void)tag;
                kuma::get_renderer().set_model_matrix(transform.model_matrix());
                kuma::get_renderer().draw();
            }

            kuma::get_renderer().set_pipeline(0);
        }

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
