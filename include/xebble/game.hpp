/// @file game.hpp
/// @brief Game configuration and the top-level `run()` entry point.
///
/// `xebble::run()` is the single function that bootstraps the entire engine:
/// it creates the window and Vulkan context, loads all assets, installs the
/// built-in systems (input, sprite rendering, UI), and then drives the game
/// loop until the window is closed or an error terminates it.
///
/// ## Minimal roguelike bootstrap
///
/// @code
/// #include <xebble/xebble.hpp>
/// using namespace xebble;
///
/// int main() {
///     // Describe every component type the game uses.
///     World world;
///     world.register_component<Position>();
///     world.register_component<Sprite>();
///     world.register_component<Health>();
///     world.register_component<Monster>();
///
///     // Register game-specific systems (called every fixed tick or frame).
///     world.add_system<MonsterAISystem>();
///     world.add_system<CombatSystem>();
///     world.add_system<DungeonRenderSystem>();
///
///     GameConfig cfg{
///         .window = {
///             .title  = "My Roguelike",
///             .width  = 1280,
///             .height = 720,
///         },
///         .renderer = {
///             .max_sprites = 65536,
///         },
///         .assets = {
///             .directory = "assets",
///             .archive   = "assets.zip",
///             .manifest  = "assets/manifest.toml",
///         },
///     };
///
///     return xebble::run(std::move(world), cfg);
/// }
/// @endcode
///
/// ## Game loop semantics
///
/// `run()` implements a **fixed-timestep update with variable rendering**:
///
/// - `System::update(world, dt)` is called once per fixed tick (default 60 Hz).
///   Use this for physics, AI, and any logic that must be frame-rate independent.
/// - `System::draw(world, renderer)` is called once per rendered frame.
///   Use this to submit draw calls to the renderer.
///
/// Input events are collected each frame and forwarded to systems via the
/// World's event queue before `update()` is called.
///
/// ## Accessing engine resources from systems
///
/// The engine stores the `AssetManager`, `Renderer`, and `Window` as World
/// resources so systems can retrieve them:
///
/// @code
/// void MyRenderSystem::draw(World& world, Renderer& renderer) {
///     const auto& assets  = world.resource<AssetManager>();
///     const auto& tileset = assets.get<SpriteSheet>("dungeon");
///
///     for (auto [e, pos, spr] : world.view<Position, Sprite>()) {
///         auto region = tileset.sprite_at(spr.col, spr.row);
///         renderer.draw_sprite(region, pos.to_vec2());
///     }
/// }
/// @endcode
#pragma once

#include <xebble/asset_manager.hpp>
#include <xebble/config.hpp>
#include <xebble/renderer.hpp>
#include <xebble/scene.hpp>
#include <xebble/types.hpp>
#include <xebble/window.hpp>
#include <xebble/world.hpp>

#include <filesystem>

namespace xebble {

/// @brief Full configuration for a Xebble game session.
///
/// Bundles together the window, renderer, and asset settings that `run()`
/// needs to bootstrap the engine. All sub-configurations have sensible defaults
/// so you only need to fill in the values relevant to your game.
///
/// @code
/// GameConfig cfg{
///     .window = {
///         .title  = "Catacombs of Dread",
///         .width  = 1280,
///         .height = 800,
///         .resizable = false,
///     },
///     .renderer = {
///         .max_sprites = 32768,
///     },
///     .assets = {
///         .directory = "assets",
///         .manifest  = "assets/manifest.toml",
///     },
/// };
/// @endcode
struct GameConfig {
    WindowConfig window;     ///< Window title, size, and display options.
    RendererConfig renderer; ///< Renderer capacity and pipeline settings.
    AssetConfig assets;      ///< Manifest, directory, and optional archive paths.
};

/// @brief Create built-in systems and run the main game loop.
///
/// This is the top-level entry point for every Xebble game. It:
///
/// 1. Opens the window and initialises Vulkan.
/// 2. Creates the `AssetManager` and loads all manifest assets.
/// 3. Installs built-in systems (sprite batch, UI input/flush, etc.).
/// 4. Stores the `Window`, `Renderer`, and `AssetManager` as World resources.
/// 5. Runs the loop: poll input → update(dt) → draw → present.
/// 6. Destroys all GPU resources and closes the window on exit.
///
/// @param world   ECS world pre-populated with component registrations and
///                game-specific systems. Ownership is transferred to `run()`.
/// @param config  Full engine configuration (window, renderer, assets).
/// @return        0 on clean exit, non-zero if a fatal error occurred.
///
/// ### Typical main.cpp
///
/// @code
/// int main() {
///     World world;
///
///     // Register all component types.
///     world.register_component<Position>();
///     world.register_component<Velocity>();
///     world.register_component<Sprite>();
///     world.register_component<Health>();
///
///     // Add game systems in execution order.
///     world.add_system<InputHandlerSystem>();
///     world.add_system<PhysicsSystem>();
///     world.add_system<CombatSystem>();
///     world.add_system<DungeonRenderSystem>();
///
///     GameConfig cfg{
///         .window   = { .title = "Dungeon Delve", .width = 1280, .height = 720 },
///         .renderer = { .max_sprites = 65536 },
///         .assets   = {
///             .directory = "assets",
///             .archive   = "assets.zip",
///             .manifest  = "assets/manifest.toml",
///         },
///     };
///
///     return xebble::run(std::move(world), cfg);
/// }
/// @endcode
int run(World world, const GameConfig& config);

/// @brief Run a multi-scene game using a scene stack.
///
/// This overload is for games that need more than one scene (title screen,
/// gameplay, pause menu, game-over screen, etc.). Instead of a single `World`,
/// you provide a `SceneRouter` — a registry of named scene factories — and
/// `run()` manages a stack of live scenes, processing `SceneTransition`
/// commands posted by systems between fixed-update ticks.
///
/// The engine bootstraps only once (window, renderer, asset manager). Each
/// time a new scene is pushed or replaced, the engine injects its resources
/// (Renderer, AssetManager, EventQueue, Camera, UIContext, etc.) into the
/// freshly-built World before calling `init_systems()`.
///
/// @param router  Registry of named scene factories plus the initial scene.
/// @param config  Full engine configuration (window, renderer, assets).
/// @return        0 on clean exit (stack drained or window closed),
///                non-zero on a fatal startup error.
///
/// ### Minimal multi-scene main.cpp
///
/// @code
/// int main() {
///     SceneRouter router;
///
///     router.add_scene("title", [](std::any) {
///         World w;
///         w.add_system<TitleSystem>();
///         return w;
///     });
///
///     router.add_scene("gameplay", [](std::any payload) {
///         int seed = payload.has_value() ? std::any_cast<int>(payload) : 42;
///         World w;
///         w.register_component<Position>();
///         w.register_component<Sprite>();
///         w.add_resource(Rng(seed));
///         w.add_system<DungeonSystem>();
///         w.add_system<PlayerSystem>();
///         return w;
///     });
///
///     router.add_scene("pause", [](std::any) {
///         World w;
///         w.add_system<PauseMenuSystem>();
///         return w;
///     });
///
///     router.set_initial("title");
///
///     GameConfig cfg{
///         .window   = { .title = "My Roguelike", .width = 1280, .height = 720 },
///         .renderer = { .max_sprites = 65536 },
///         .assets   = { .directory = "assets", .manifest = "assets/manifest.toml" },
///     };
///     return xebble::run(std::move(router), cfg);
/// }
/// @endcode
///
/// ### Requesting transitions from a system
///
/// @code
/// // In TitleSystem::update() — start a new game when Enter is pressed.
/// void TitleSystem::update(World& world, float) {
///     for (auto& ev : world.resource<EventQueue>().events) {
///         if (auto* k = std::get_if<KeyEvent>(&ev)) {
///             if (k->key == Key::Enter && k->action == Action::Press) {
///                 world.resource<SceneTransition>() =
///                     SceneTransition::replace("gameplay", 12345 /*seed*/);
///             }
///         }
///     }
/// }
///
/// // In PlayerSystem::update() — push pause menu on Escape.
/// void PlayerSystem::update(World& world, float) {
///     for (auto& ev : world.resource<EventQueue>().events) {
///         if (auto* k = std::get_if<KeyEvent>(&ev)) {
///             if (k->key == Key::Escape && k->action == Action::Press) {
///                 world.resource<SceneTransition>() =
///                     SceneTransition::push("pause", {}, DrawBelow::Yes);
///             }
///         }
///     }
/// }
/// @endcode
int run(SceneRouter router, const GameConfig& config);

/// @brief Run a single-world game using a TOML configuration file.
///
/// Convenience overload: loads the given TOML file with `Config::load()`,
/// converts to a `GameConfig`, and calls `run(World, GameConfig)`.  The
/// returned `Config` is also injected as a World resource so systems can
/// read game-specific values from the `[game]` section.
///
/// @code
/// int main() {
///     World world;
///     world.add_system<MyGame>();
///     return xebble::run(std::move(world), "game.toml");
/// }
/// @endcode
int run(World world, const std::filesystem::path& config_path);

/// @brief Run a scene-stack game using a TOML configuration file.
int run(SceneRouter router, const std::filesystem::path& config_path);

} // namespace xebble
