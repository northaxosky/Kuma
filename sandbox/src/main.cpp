#include <kuma/kuma.h>

int main() {
    kuma::EngineConfig config{};
    config.app_name = "Kuma Sandbox";
    config.window_width = 1920;
    config.window_height = 1080;

    if (!kuma::init(config)) {
        return 1;
    }

    kuma::Window& window = kuma::get_window();
    kuma::Renderer& renderer = kuma::get_renderer();

    while (window.poll_events()) {
        if (renderer.begin_frame()) {
            renderer.end_frame();
        }
    }

    kuma::shutdown();
    return 0;
}
