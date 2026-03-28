#pragma once

#include <cstdint>

struct SDL_Window;

namespace kuma {

struct RendererConfig {
    SDL_Window* window = nullptr;
    int32_t width = 1920;
    int32_t height = 1080;
    bool enable_validation = true;
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

    void on_resize(int32_t width, int32_t height);

private:
    RendererImpl* impl_ = nullptr;
};

} // namespace kuma
