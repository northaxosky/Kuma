#include <kuma/input.h>
#include <kuma/kuma.h>
#include <kuma/log.h>
#include <kuma/time.h>

#include <SDL3/SDL.h>

namespace kuma {

// Internal — not in time.h because game code must not call it.
namespace time { void tick(); }

static Window s_window;
static Renderer s_renderer;
static ResourceManager s_resource_manager;

bool init(const EngineConfig& config) {
    kuma::log::info("Initializing engine: %s", config.app_name);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        kuma::log::error("Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }

    if (!input::init()) {
        SDL_Quit();
        return false;
    }

    WindowConfig window_config{};
    window_config.title = config.app_name;
    window_config.width = config.window_width;
    window_config.height = config.window_height;

    if (!s_window.create(window_config)) {
        input::shutdown();
        SDL_Quit();
        return false;
    }

    RendererConfig renderer_config{};
    renderer_config.window = s_window.native_handle();
    renderer_config.width = config.window_width;
    renderer_config.height = config.window_height;
    renderer_config.enable_validation = config.enable_validation;
    renderer_config.present_mode = config.present_mode;

    s_window.set_resize_callback([](int32_t w, int32_t h) { s_renderer.on_resize(w, h); });

    if (!s_renderer.init(renderer_config)) {
        s_window.destroy();
        input::shutdown();
        SDL_Quit();
        return false;
    }

    if (!s_resource_manager.init(s_renderer.gpu_context())) {
        s_renderer.shutdown();
        s_window.destroy();
        input::shutdown();
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
        input::shutdown();
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
    input::shutdown();
    SDL_Quit();
    kuma::log::info("Engine shut down");
}

// ── Frame orchestration ─────────────────────────────────────────
// This is the *only* place frame phase ordering is hardcoded —
// modules must not call each other's frame hooks directly.

bool begin_frame() {
    // Phase 1: INPUT — Window::poll_events drains SDL events and
    // snapshots input state. Returns false on quit request.
    if (!s_window.poll_events()) {
        return false;
    }

    // Phase 2: TIME — sample steady_clock and update the global
    // Clock. After this returns, kuma::time::delta() is the
    // duration of the previous frame (0 on the first frame).
    time::tick();

    return true;
}

void end_frame() {
    // Phase 4: RENDER + Phase 5: PRESENT — wrapped together because
    // begin_frame() can return false on swapchain rebuild, in which
    // case we must skip end_frame() to keep the renderer's internal
    // state consistent.
    if (s_renderer.begin_frame()) {
        s_renderer.end_frame();
    }
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
