#pragma once

// Kuma Engine — main include header
// Game code only needs: #include <kuma/kuma.h>

#include <kuma/window.h>
#include <kuma/renderer.h>
#include <kuma/resource_manager.h>

namespace kuma {

struct EngineConfig {
    const char* app_name = "Kuma App";
    int32_t window_width = 1920;
    int32_t window_height = 1080;
    bool enable_validation = true;
};

// Initialize the engine. Call once at startup.
bool init(const EngineConfig& config);

// Shut down the engine. Call once before exit.
void shutdown();

// Access engine subsystems (valid between init and shutdown)
Window& get_window();
Renderer& get_renderer();
ResourceManager& get_resource_manager();

} // namespace kuma
