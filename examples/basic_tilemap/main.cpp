#include <xebble/window.hpp>
#include <cstdio>

int main() {
    auto window_result = xebble::Window::create({
        .title = "Xebble - Basic Tilemap",
        .width = 1280,
        .height = 720,
    });

    if (!window_result) {
        std::fprintf(stderr, "Failed to create window: %s\n",
                     window_result.error().message.c_str());
        return 1;
    }

    auto& window = *window_result;

    while (!window.should_close()) {
        window.poll_events();
        for (auto& event : window.events()) {
            switch (event.type) {
                case xebble::EventType::KeyPress:
                    std::printf("Key pressed: %d\n", static_cast<int>(event.key().key));
                    break;
                case xebble::EventType::WindowResize:
                    std::printf("Resized: %ux%u\n", event.resize().width, event.resize().height);
                    break;
                default:
                    break;
            }
        }
    }

    return 0;
}
