#pragma once

// Kuma Engine — main include header
// Game code only needs: #include <kuma/kuma.h>

#include <kuma/window.h>

namespace kuma {

struct EngineConfig {
    const char* app_name = "Kuma App";
    int32_t window_width = 1920;
    int32_t window_height = 1080;
};

// Initialize the engine. Call once at startup.
bool init(const EngineConfig& config);

// Shut down the engine. Call once before exit.
void shutdown();

// Access the engine's window (valid between init and shutdown)
Window& get_window();

} // namespace kuma
