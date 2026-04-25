#pragma once

#include <cstdint>
#include <functional>

// Forward-declare SDL types so this header doesn't pull in SDL.h
// Game code includes kuma/window.h but never needs to know about SDL
struct SDL_Window;

namespace kuma {

struct WindowConfig {
    const char* title = "Kuma App";
    int32_t width = 1920;
    int32_t height = 1080;
};

class Window {
public:
    Window() = default;
    ~Window();

    // Non-copyable — there's only one real window
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Movable — ownership can transfer
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    bool create(const WindowConfig& config);
    void destroy();

    // Process OS events (keyboard, mouse, close button, etc.)
    // Returns false when the user requests quit (clicks X)
    bool poll_events();

    using ResizeCallback = std::function<void(int32_t, int32_t)>;
    void set_resize_callback(ResizeCallback callback) { resize_callback_ = std::move(callback); }

    // Enables/disables relative mouse mode for this window. In relative
    // mode SDL hides the cursor and reports unlimited mouse deltas even
    // when the physical cursor would have hit a screen edge.
    bool set_relative_mouse_mode(bool enabled);

    SDL_Window* native_handle() const { return window_; }
    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
    bool is_open() const { return window_ != nullptr; }

private:
    SDL_Window* window_ = nullptr;
    int32_t width_ = 0;
    int32_t height_ = 0;
    ResizeCallback resize_callback_;
};

}  // namespace kuma
