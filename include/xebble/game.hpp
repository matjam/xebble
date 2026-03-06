/// @file game.hpp
/// @brief Game base class and run function for structured game loops.
///
/// Provides a Game base class with init/update/draw/layout/on_event/shutdown
/// hooks, and a run() function that manages Window, Renderer, and AssetManager
/// with a fixed-timestep update loop.
#pragma once

#include <xebble/types.hpp>
#include <xebble/window.hpp>
#include <xebble/renderer.hpp>
#include <xebble/asset_manager.hpp>
#include <memory>

namespace xebble {

class Event;

/// @brief Base class for games using the Xebble framework.
///
/// Subclass and override the virtual methods. Pass to run() to start.
class Game {
public:
    virtual ~Game() = default;

    /// @brief Called once after all systems are initialized.
    virtual void init(Renderer& renderer, AssetManager& assets) = 0;

    /// @brief Called at fixed timestep intervals for game logic.
    /// @param dt Fixed timestep duration in seconds.
    virtual void update(float dt) = 0;

    /// @brief Called each frame to submit draw calls.
    virtual void draw(Renderer& renderer) = 0;

    /// @brief Called when the virtual resolution or window size changes.
    virtual void layout(uint32_t width, uint32_t height) = 0;

    /// @brief Called for each input event.
    virtual void on_event(const Event& event) {}

    /// @brief Called before shutdown for cleanup.
    virtual void shutdown() {}
};

/// @brief Configuration for the game loop.
struct GameConfig {
    WindowConfig window;                    ///< Window configuration.
    RendererConfig renderer;                ///< Renderer configuration.
    AssetConfig assets;                     ///< Asset manager configuration.
    float fixed_timestep = 1.0f / 60.0f;   ///< Fixed update timestep in seconds.
};

/// @brief Create systems and run the game loop.
/// @param game The game instance.
/// @param config Full game configuration.
/// @return 0 on success, non-zero on failure.
int run(std::unique_ptr<Game> game, const GameConfig& config);

} // namespace xebble
