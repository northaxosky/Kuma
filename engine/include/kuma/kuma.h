#pragma once

// Kuma Engine — main include header
// Game code only needs: #include <kuma/kuma.h>

#include <kuma/input.h>
#include <kuma/renderer.h>
#include <kuma/resource_manager.h>
#include <kuma/time.h>
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
};

// Initialize the engine. Call once at startup.
bool init(const EngineConfig& config);

// Shut down the engine. Call once before exit.
void shutdown();

// ── Frame orchestration ────────────────────────────────────────────
// The canonical Kuma frame is split into 5 phases. Each module
// declares the phase it implements; modules may only depend on data
// produced by *earlier* phases. This file is the single source of
// truth for frame ordering.
//
//   Phase 1  INPUT    drain OS events, snapshot input state
//   Phase 2  TIME     tick the clock, compute delta_time      (TBD)
//   Phase 3  UPDATE   game logic — runs in the caller's loop body
//   Phase 4  RENDER   record draw commands
//   Phase 5  PRESENT  submit + swap
//
// Game-owned loop pattern (Tier 1 orchestration):
//
//     while (kuma::begin_frame()) {  // phases 1-2
//         // phase 3: your game update goes here
//         kuma::end_frame();         // phases 4-5
//     }
//
// Returns false when the user requests quit (window close, etc.).
bool begin_frame();
void end_frame();

// Access engine subsystems (valid between init and shutdown)
Window& get_window();
Renderer& get_renderer();
ResourceManager& get_resource_manager();

}  // namespace kuma
