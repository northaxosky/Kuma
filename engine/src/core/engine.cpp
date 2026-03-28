#include <kuma/kuma.h>
#include <cstdio>

namespace kuma {

bool init(const EngineConfig& config) {
    std::printf("[Kuma] Initializing engine: %s (%dx%d)\n",
        config.app_name, config.window_width, config.window_height);
    // TODO: Initialize platform layer (SDL window)
    // TODO: Initialize renderer (Vulkan)
    return true;
}

void shutdown() {
    std::printf("[Kuma] Shutting down engine\n");
    // TODO: Destroy renderer
    // TODO: Destroy platform layer
}

} // namespace kuma
