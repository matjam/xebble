/// @file game.cpp
/// @brief Game loop implementation.
#include <xebble/game.hpp>
#include <xebble/event.hpp>
#include <xebble/log.hpp>
#include <xebble/components.hpp>
#include <xebble/builtin_systems.hpp>
#include <xebble/ui.hpp>
#include <xebble/embedded_font.hpp>

namespace xebble {

int run(World world, const GameConfig& config) {
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

    // Add engine resources
    world.add_resource<Renderer*>(&*renderer);
    world.add_resource<AssetManager*>(&*assets);
    world.add_resource<EventQueue>(EventQueue{});

    // Register built-in components
    world.register_component<Position>();
    world.register_component<Sprite>();
    world.register_component<TileMapLayer>();

    // Add default Camera resource if not already provided
    if (!world.has_resource<Camera>()) {
        world.add_resource<Camera>(Camera{});
    }

    // Append built-in render systems (run after user systems)
    world.add_system<TileMapRenderSystem>();
    world.add_system<SpriteRenderSystem>();

    // --- UI system auto-registration ---
    std::unique_ptr<BitmapFont> embedded_font_ptr;
    if (!world.has_resource<UITheme>()) {
        auto font_result = embedded_font::create_font(renderer->context());
        if (!font_result) {
            log(LogLevel::Error, "Failed to create embedded font: " + font_result.error().message);
            return 1;
        }
        embedded_font_ptr = std::move(*font_result);

        UITheme default_theme;
        default_theme.font = embedded_font_ptr.get();
        world.add_resource<UITheme>(default_theme);
    }

    struct EmbeddedFontStorage {
        std::shared_ptr<BitmapFont> font;
    };
    if (embedded_font_ptr) {
        world.add_resource<EmbeddedFontStorage>({std::move(embedded_font_ptr)});
    }

    world.add_resource<UIContext>(UIContext{});
    world.prepend_system<UIInputSystem>();
    world.add_system<UIFlushSystem>();

    world.init_systems();

    float accumulator = 0.0f;

    while (!window->should_close()) {
        window->poll_events();
        auto raw = window->events();
        world.resource<EventQueue>().events.assign(raw.begin(), raw.end());

        accumulator += renderer->delta_time();
        while (accumulator >= config.fixed_timestep) {
            world.tick_update(config.fixed_timestep);
            accumulator -= config.fixed_timestep;
        }

        if (renderer->begin_frame()) {
            world.tick_draw(*renderer);
            renderer->end_frame();
        }
    }

    return 0;
}

} // namespace xebble
