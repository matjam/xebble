/// @file components.hpp
/// @brief Built-in ECS components and resources provided by xebble.
#pragma once

#include <xebble/types.hpp>
#include <xebble/tilemap.hpp>

#include <memory>

namespace xebble {

class SpriteSheet;

/// @brief World-space position in pixels.
struct Position {
    float x = 0;
    float y = 0;
};

/// @brief Sprite rendering component. Attach to an entity with Position to draw it.
struct Sprite {
    const SpriteSheet* sheet = nullptr;
    uint32_t tile_index = 0;
    float z_order = 0.0f;
    Color tint = {255, 255, 255, 255};
};

/// @brief TileMap rendering component. Attach to an entity to draw a tilemap layer.
struct TileMapLayer {
    std::shared_ptr<TileMap> tilemap;
    float z_order = 0.0f;
};

/// @brief Camera resource. Top-left corner of the viewport in world pixels.
struct Camera {
    float x = 0;
    float y = 0;
};

} // namespace xebble
