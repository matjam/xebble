/// @file game.cpp
/// @brief Game loop implementation.
#include <xebble/game.hpp>
#include <xebble/event.hpp>
#include <xebble/log.hpp>

namespace xebble {

int run(std::unique_ptr<Game> game, const GameConfig& config) {
    auto window = Window::create(config.window);
    if (!window) {
        log(LogLevel::Error, "Failed to create window: " + window.error().message);
        return 1;
    }

    auto renderer = Renderer::create(*window, config.renderer);
    if (!renderer) {
        log(LogLevel::Error, "Failed to create renderer: " + renderer.error().message);
        return 1;
    }

    auto assets = AssetManager::create(renderer->context(), config.assets);
    if (!assets) {
        log(LogLevel::Error, "Failed to create asset manager: " + assets.error().message);
        return 1;
    }

    game->init(*renderer, *assets);
    game->layout(config.renderer.virtual_width, config.renderer.virtual_height);

    float accumulator = 0.0f;

    while (!window->should_close()) {
        window->poll_events();
        for (auto& event : window->events()) {
            if (event.type == EventType::WindowResize) {
                game->layout(event.resize().width, event.resize().height);
            }
            game->on_event(event);
        }

        accumulator += renderer->delta_time();
        while (accumulator >= config.fixed_timestep) {
            game->update(config.fixed_timestep);
            accumulator -= config.fixed_timestep;
        }

        if (renderer->begin_frame()) {
            game->draw(*renderer);
            renderer->end_frame();
        }
    }

    game->shutdown();
    return 0;
}

} // namespace xebble
