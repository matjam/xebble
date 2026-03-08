/// @file game.cpp
/// @brief Game loop implementation — single-world and scene-stack variants.
#include <xebble/builtin_systems.hpp>
#include <xebble/components.hpp>
#include <xebble/embedded_font.hpp>
#include <xebble/event.hpp>
#include <xebble/game.hpp>
#include <xebble/log.hpp>
#include <xebble/scene.hpp>
#include <xebble/ui.hpp>

#include <optional>

namespace xebble {

// Keeps the embedded BitmapFont alive for the lifetime of the World that owns it.
// shared_ptr so the struct is copy-constructible (required by std::any).
struct EmbeddedFontStorage {
    std::shared_ptr<BitmapFont> font;
};

// ---------------------------------------------------------------------------
// Internal helper: bootstrap a Window + Renderer + AssetManager.
// ---------------------------------------------------------------------------

struct EngineHandle {
    std::optional<Window> window;
    std::optional<Renderer> renderer;
    std::optional<AssetManager> assets;
    bool ok = false;
};

static EngineHandle make_engine(const GameConfig& config) {
    EngineHandle h;

    auto win = Window::create(config.window);
    if (!win) {
        log(LogLevel::Error, "Failed to create window: " + win.error().message);
        return h;
    }
    h.window = std::move(*win);

    auto ren = Renderer::create(*h.window, config.renderer);
    if (!ren) {
        log(LogLevel::Error, "Failed to create renderer: " + ren.error().message);
        return h;
    }
    h.renderer = std::move(*ren);

    auto ast = AssetManager::create(h.renderer->context(), config.assets);
    if (!ast) {
        log(LogLevel::Error, "Failed to create asset manager: " + ast.error().message);
        return h;
    }
    h.assets = std::move(*ast);

    h.ok = true;
    return h;
}

// ---------------------------------------------------------------------------
// Internal helper: inject engine resources + built-in systems into a World,
// then call init_systems(). The embedded font is uploaded once and shared
// across all scene Worlds via a shared_ptr so:
//   (a) it is only uploaded to the GPU once, and
//   (b) every World gets its own EmbeddedFontStorage (keeping the shared_ptr
//       refcount > 0) so world.resource<EmbeddedFontStorage>() is always valid.
// ---------------------------------------------------------------------------

static bool inject_and_init(World& world, Renderer& renderer, AssetManager& assets,
                            std::shared_ptr<BitmapFont>& shared_font) {
    // Engine resources
    world.add_resource<Renderer*>(&renderer);
    world.add_resource<AssetManager*>(&assets);
    if (!world.has_resource<EventQueue>())
        world.add_resource<EventQueue>(EventQueue{});

    // Built-in components
    world.register_component<Position>();
    world.register_component<Sprite>();
    world.register_component<TileMapLayer>();

    // Default camera
    if (!world.has_resource<Camera>())
        world.add_resource<Camera>(Camera{});

    // Scene transition resource (needed by SceneStack; harmless for bare Worlds)
    if (!world.has_resource<SceneTransition>())
        world.add_resource<SceneTransition>(SceneTransition{});

    // Built-in render systems (appended after user systems)
    world.add_system<TileMapRenderSystem>();
    world.add_system<SpriteRenderSystem>();

    // UI auto-registration
    if (!world.has_resource<UITheme>()) {
        // Upload the font to the GPU exactly once; reuse the shared_ptr for
        // every subsequent scene World so we don't upload duplicate textures.
        if (!shared_font) {
            auto font_result = embedded_font::create_font(renderer.context());
            if (!font_result) {
                log(LogLevel::Error,
                    "Failed to create embedded font: " + font_result.error().message);
                return false;
            }
            shared_font = std::move(*font_result);
        }

        // Give this World its own EmbeddedFontStorage wrapping the shared font.
        // Every World needs the resource so world.resource<EmbeddedFontStorage>()
        // is valid — even on scene 2, 3, … where the font was already uploaded.
        world.add_resource<EmbeddedFontStorage>(EmbeddedFontStorage{shared_font});

        UITheme default_theme;
        default_theme.font = shared_font.get();
        world.add_resource<UITheme>(default_theme);
    }

    world.add_resource<UIContext>(UIContext{});
    world.resource<UIContext>().set_theme(&world.resource<UITheme>());
    world.prepend_system<UIInputSystem>();
    world.add_system<UIFlushSystem>();

    world.init_systems();
    return true;
}

// ---------------------------------------------------------------------------
// run(World) — single-world, no scene stack
// ---------------------------------------------------------------------------

int run(World world, const GameConfig& config) {
    auto h = make_engine(config);
    if (!h.ok)
        return 1;

    std::shared_ptr<BitmapFont> shared_font;
    if (!inject_and_init(world, *h.renderer, *h.assets, shared_font))
        return 1;

    while (!h.window->should_close()) {
        h.window->poll_events();
        auto raw = h.window->events();

        world.resource<EventQueue>().events.assign(raw.begin(), raw.end());

        if (h.renderer->begin_frame()) {
            float dt = h.renderer->delta_time();
            world.tick_update(dt);
            world.tick_draw(*h.renderer);
            h.renderer->end_frame();
        }
    }

    // Explicitly destroy the world before the engine handle so that all
    // GPU resources owned by systems/components (textures, spritesheets,
    // the embedded font) are freed while the VMA allocator is still alive.
    // Without this, the World (a function parameter) would outlive the
    // local EngineHandle and attempt to free VMA allocations after
    // vmaDestroyAllocator() has already run, causing an abort.
    { World drop = std::move(world); }

    return 0;
}

// ---------------------------------------------------------------------------
// run(SceneRouter) — scene-stack variant
// ---------------------------------------------------------------------------

int run(SceneRouter router, const GameConfig& config) {
    auto h = make_engine(config);
    if (!h.ok)
        return 1;

    std::shared_ptr<BitmapFont> shared_font;

    // The injector lambda captures engine singletons and calls inject_and_init
    // on each freshly built World before it starts ticking.
    auto injector = [&](World& world) -> bool {
        return inject_and_init(world, *h.renderer, *h.assets, shared_font);
    };

    SceneStack stack(router);

    // Bootstrap the initial scene.
    bool init_ok = true;
    stack.push_initial([&](World& w) {
        if (!injector(w))
            init_ok = false;
    });
    if (!init_ok)
        return 1;

    while (!h.window->should_close() && !stack.empty()) {
        h.window->poll_events();
        auto raw = h.window->events();

        stack.top_world().resource<EventQueue>().events.assign(raw.begin(), raw.end());

        if (!stack.empty() && h.renderer->begin_frame()) {
            float dt = h.renderer->delta_time();
            stack.tick_update(dt);
            // Apply any pending transition after the tick.
            stack.apply_transition([&](World& w) {
                if (!injector(w))
                    init_ok = false;
            });
            if (!init_ok)
                return 1;
            if (!stack.empty()) {
                stack.tick_draw(*h.renderer);
            }
            h.renderer->end_frame();
        }
    }

    // Explicitly destroy the scene stack (and all its Worlds) before the
    // engine handle so GPU resources are freed while VMA is still alive.
    { SceneStack drop = std::move(stack); }

    return 0;
}

} // namespace xebble
