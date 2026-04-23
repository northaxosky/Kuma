#include <kuma/log.h>
#include <kuma/window.h>

#include "platform/input_internal.h"

#include <SDL3/SDL.h>

namespace kuma {

Window::~Window() {
    destroy();
}

Window::Window(Window&& other) noexcept
    : window_(other.window_), width_(other.width_), height_(other.height_) {
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
    window_ = SDL_CreateWindow(config.title, config.width, config.height,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    if (!window_) {
        kuma::log::error("Failed to create window: %s", SDL_GetError());
        return false;
    }

    width_ = config.width;
    height_ = config.height;

    kuma::log::info("Window created: %s (%dx%d)", config.title, width_, height_);
    return true;
}

void Window::destroy() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        kuma::log::info("Window destroyed");
    }
}

bool Window::poll_events() {
    // Implements frame Phase 1 (INPUT). See <kuma/kuma.h> phase
    // contract. Snapshots input state for edge detection BEFORE
    // draining new events: the events about to flow in belong to
    // the new frame's `current` snapshot.
    input::begin_frame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Forward every event to the input system. It picks out the ones
        // it cares about (keyboard, mouse) and ignores the rest.
        input::process_sdl_event(event);

        switch (event.type) {
        case SDL_EVENT_QUIT:
            return false;

        case SDL_EVENT_WINDOW_RESIZED:
            width_ = event.window.data1;
            height_ = event.window.data2;
            if (resize_callback_) {
                resize_callback_(width_, height_);
            }
            break;
        }
    }
    return true;
}

}  // namespace kuma
