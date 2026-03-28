#pragma once

// Kuma Engine — main include header
// As we build systems, this header will include all public engine APIs.
// Game code only needs: #include <kuma/kuma.h>

namespace kuma {

struct EngineConfig {
    const char* app_name = "Kuma App";
    int window_width = 1920;
    int window_height = 1080;
};

// Initialize the engine. Call once at startup.
bool init(const EngineConfig& config);

// Shut down the engine. Call once before exit.
void shutdown();

} // namespace kuma
