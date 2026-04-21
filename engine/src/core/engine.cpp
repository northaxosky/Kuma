#include <kuma/kuma.h>
#include <kuma/log.h>

#include <SDL3/SDL.h>

namespace kuma {

static Window s_window;
static Renderer s_renderer;
static ResourceManager s_resource_manager;

bool init(const EngineConfig& config) {
    kuma::log::info("Initializing engine: %s", config.app_name);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        kuma::log::error("Failed to initialize SDL: %s", SDL_GetError());
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

    s_window.set_resize_callback([](int32_t w, int32_t h) { s_renderer.on_resize(w, h); });

    if (!s_renderer.init(renderer_config)) {
        s_window.destroy();
        SDL_Quit();
        return false;
    }

    if (!s_resource_manager.init(s_renderer.gpu_context())) {
        s_renderer.shutdown();
        s_window.destroy();
        SDL_Quit();
        return false;
    }

    // Load default resources via the resource manager
    const auto* mesh = s_resource_manager.load_mesh("assets/models/quad.obj");
    const auto* texture = s_resource_manager.load_texture("assets/textures/VaultBoyNV.png");

    if (!mesh || !texture) {
        kuma::log::error("Failed to load default resources");
        s_resource_manager.shutdown();
        s_renderer.shutdown();
        s_window.destroy();
        SDL_Quit();
        return false;
    }

    s_renderer.set_mesh(mesh);
    s_renderer.set_texture(texture);

    return true;
}

void shutdown() {
    s_renderer.wait_idle();         // GPU finishes all work
    s_resource_manager.shutdown();  // safe to destroy textures/meshes
    s_renderer.shutdown();          // destroy Vulkan device last
    s_window.destroy();
    SDL_Quit();
    kuma::log::info("Engine shut down");
}

Window& get_window() {
    return s_window;
}

Renderer& get_renderer() {
    return s_renderer;
}

ResourceManager& get_resource_manager() {
    return s_resource_manager;
}

}  // namespace kuma
