#include <kuma/kuma.h>

int main() {
    kuma::EngineConfig config{};
    config.app_name = "Kuma Sandbox";
    config.window_width = 1920;
    config.window_height = 1080;

    if (!kuma::init(config)) {
        return 1;
    }

    // The game loop: runs every frame until the window is closed
    kuma::Window& window = kuma::get_window();
    while (window.poll_events()) {
        // TODO: update game logic
        // TODO: render frame
    }

    kuma::shutdown();
    return 0;
}
