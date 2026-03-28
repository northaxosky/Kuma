#include <kuma/kuma.h>
#include <SDL3/SDL.h>
#include <cstdio>

namespace kuma {

// Engine-owned window — lives for the lifetime of the engine
static Window s_window;

bool init(const EngineConfig& config) {
    std::printf("[Kuma] Initializing engine: %s\n", config.app_name);

    // SDL_Init sets up the video subsystem (display, windows, events)
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("[Kuma] Failed to initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    WindowConfig window_config{};
    window_config.title = config.app_name;
    window_config.width = config.window_width;
    window_config.height = config.window_height;

    if (!s_window.create(window_config)) {
        SDL_Quit();
        return false;
    }

    return true;
}

void shutdown() {
    s_window.destroy();
    SDL_Quit();
    std::printf("[Kuma] Engine shut down\n");
}

Window& get_window() {
    return s_window;
}

} // namespace kuma
