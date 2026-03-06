/// @file game.hpp
/// @brief Game configuration and run function.
#pragma once

#include <xebble/types.hpp>
#include <xebble/window.hpp>
#include <xebble/renderer.hpp>
#include <xebble/asset_manager.hpp>
#include <xebble/world.hpp>

namespace xebble {

/// @brief Configuration for the game loop.
struct GameConfig {
    WindowConfig window;                    ///< Window configuration.
    RendererConfig renderer;                ///< Renderer configuration.
    AssetConfig assets;                     ///< Asset manager configuration.
    float fixed_timestep = 1.0f / 60.0f;   ///< Fixed update timestep in seconds.
};

/// @brief Create systems and run the game loop with an ECS World.
/// @param world The ECS world with registered components and systems.
/// @param config Full game configuration.
/// @return 0 on success, non-zero on failure.
int run(World world, const GameConfig& config);

} // namespace xebble
