#include <kuma/kuma.h>

int main() {
    kuma::EngineConfig config{};
    config.app_name = "Kuma Sandbox";
    config.window_width = 1920;
    config.window_height = 1080;

    if (!kuma::init(config)) {
        return 1;
    }

    // TODO: Main game loop will go here
    // while (kuma::is_running()) {
    //     kuma::poll_events();
    //     kuma::begin_frame();
    //     kuma::end_frame();
    // }

    kuma::shutdown();
    return 0;
}
