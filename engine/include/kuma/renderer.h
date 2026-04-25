#pragma once

#include <kuma/math.h>

#include <cstdint>

struct SDL_Window;

namespace kuma {

// ── Present mode ────────────────────────────────────────────────
// How the GPU paces frames at the swapchain. The right choice is
// genre-dependent — there is no universal best.
enum class PresentMode {
    // FIFO — vsync on. Capped to monitor refresh rate. Low power,
    // no tearing, predictable dt. Default for single-player and
    // most non-competitive games.
    Vsync,

    // MAILBOX — uncapped render rate, monitor-paced display. Newest
    // frame always shown, older queued frames discarded. Lowest
    // input latency, highest power draw. Falls back to Vsync if the
    // GPU/driver does not support mailbox.
    Mailbox,

    // IMMEDIATE — uncapped, no synchronization. May tear. Mostly
    // useful for benchmarking. Falls back to Vsync if unsupported.
    Immediate,
};

struct RendererConfig {
    SDL_Window* window = nullptr;
    int32_t width = 1920;
    int32_t height = 1080;
    bool enable_validation = true;
    PresentMode present_mode = PresentMode::Vsync;
};

// Forward declaration — the actual implementation is hidden in the .cpp
// Game code never touches Vulkan types directly
class RendererImpl;

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(const RendererConfig& config);
    void shutdown();

    bool begin_frame();
    void end_frame();

    // Wait for all GPU work to complete. Call before destroying resources.
    void wait_idle();

    void on_resize(int32_t width, int32_t height);

    // Set the active texture and mesh (loaded by ResourceManager).
    void set_texture(const void* texture);
    void set_mesh(const void* mesh);

    // Set the active camera matrix for subsequent frames. Usually called
    // during Phase 3 UPDATE after camera movement, before end_frame()
    // records Phase 4 render commands.
    void set_view_projection(const Mat4& view_projection);

    // Internal — returns opaque GPU context for resource loading.
    // Game code should not call this.
    void* gpu_context();

private:
    RendererImpl* impl_ = nullptr;
};

}  // namespace kuma
