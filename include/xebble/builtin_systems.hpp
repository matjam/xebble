/// @file builtin_systems.hpp
/// @brief Built-in ECS systems provided by xebble.
#pragma once

#include <xebble/system.hpp>

namespace xebble {

/// @brief Renders all entities with TileMapLayer, offset by Camera.
class TileMapRenderSystem : public System {
public:
    void draw(World& world, Renderer& renderer) override;
};

/// @brief Renders all entities with Position + Sprite, offset by Camera.
class SpriteRenderSystem : public System {
public:
    void draw(World& world, Renderer& renderer) override;
};

} // namespace xebble
