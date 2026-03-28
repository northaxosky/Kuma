#include <kuma/window.h>
#include <SDL3/SDL.h>
#include <cstdio>

namespace kuma {

Window::~Window() {
    destroy();
}

Window::Window(Window&& other) noexcept
    : window_(other.window_)
    , width_(other.width_)
    , height_(other.height_) {
    other.window_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        destroy();
        window_ = other.window_;
        width_ = other.width_;
        height_ = other.height_;
        other.window_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

bool Window::create(const WindowConfig& config) {
    window_ = SDL_CreateWindow(
        config.title,
        config.width,
        config.height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    if (!window_) {
        std::printf("[Kuma] Failed to create window: %s\n", SDL_GetError());
        return false;
    }

    width_ = config.width;
    height_ = config.height;

    std::printf("[Kuma] Window created: %s (%dx%d)\n",
        config.title, width_, height_);
    return true;
}

void Window::destroy() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        std::printf("[Kuma] Window destroyed\n");
    }
}

bool Window::poll_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            return false;

        case SDL_EVENT_WINDOW_RESIZED:
            width_ = event.window.data1;
            height_ = event.window.data2;
            std::printf("[Kuma] Window resized: %dx%d\n", width_, height_);
            break;
        }
    }
    return true;
}

} // namespace kuma
