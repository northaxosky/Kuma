#include <kuma/kuma.h>
#include <SDL3/SDL.h>
#include <cstdio>

namespace kuma {

static Window s_window;
static Renderer s_renderer;

bool init(const EngineConfig& config) {
    std::printf("[Kuma] Initializing engine: %s\n", config.app_name);

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

    RendererConfig renderer_config{};
    renderer_config.window = s_window.native_handle();
    renderer_config.width = config.window_width;
    renderer_config.height = config.window_height;
    renderer_config.enable_validation = config.enable_validation;

    s_window.set_resize_callback([](int32_t w, int32_t h) {
        s_renderer.on_resize(w, h);
    });

    if (!s_renderer.init(renderer_config)) {
        s_window.destroy();
        SDL_Quit();
        return false;
    }

    return true;
}

void shutdown() {
    s_renderer.shutdown();
    s_window.destroy();
    SDL_Quit();
    std::printf("[Kuma] Engine shut down\n");
}

Window& get_window() {
    return s_window;
}

Renderer& get_renderer() {
    return s_renderer;
}

} // namespace kuma
