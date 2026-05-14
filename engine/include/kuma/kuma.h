#pragma once

// Kuma Engine — main include header
// Game code only needs: #include <kuma/kuma.h>

#include <kuma/camera.h>
#include <kuma/character.h>
#include <kuma/debug.h>
#include <kuma/ecs.h>
#include <kuma/fps_camera.h>
#include <kuma/input.h>
#include <kuma/physics.h>
#include <kuma/platform.h>
#include <kuma/renderer.h>
#include <kuma/resource_manager.h>
#include <kuma/time.h>
#include <kuma/transform.h>
#include <kuma/window.h>

namespace kuma {

struct EngineConfig {
    const char* app_name = "Kuma App";
    int32_t window_width = 1920;
    int32_t window_height = 1080;
    bool enable_validation = true;

    // Frame pacing — see <kuma/renderer.h> for trade-offs. Defaults
    // to vsync; flip to PresentMode::Mailbox for competitive low-
    // latency scenarios.
    PresentMode present_mode = PresentMode::Vsync;

    // Physics tuning. Defaults match a few-hundred-body arena
    // scenario; bump body limits or temp allocator for stress tests.
    physics::PhysicsConfig physics;
};

// Initialize the engine. Call once at startup.
bool init(const EngineConfig& config);

// Shut down the engine. Call once before exit.
void shutdown();

// ── Frame loop ─────────────────────────────────────────────────────
// Game-owned main loop:
//
//     while (kuma::begin_frame()) {
//         // your game update goes here
//         kuma::end_frame();
//     }
//
// begin_frame drains OS input, ticks the clock, and prepares the
// renderer for recording. Your update code calls renderer.draw()
// once per object and any other engine APIs. end_frame submits the
// recorded commands and presents.
//
// Returns false when the user requests quit (window close, etc.).
bool begin_frame();
void end_frame();

// Access engine subsystems (valid between init and shutdown)
Window& get_window();
Renderer& get_renderer();
ResourceManager& get_resource_manager();

}  // namespace kuma
