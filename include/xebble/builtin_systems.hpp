/// @file builtin_systems.hpp
/// @brief Built-in ECS rendering systems provided by Xebble.
///
/// Register these systems in your World to get automatic rendering of tilemaps
/// and sprites. Both systems read the `Camera` resource to scroll the world.
///
/// ## Registration order
///
/// Add tilemap and sprite render systems **after** all update systems so that
/// render commands see the game state for the current frame, not last frame's:
///
/// @code
/// // Logic systems first.
/// world.add_system<PlayerInputSystem>();
/// world.add_system<MovementSystem>();
/// world.add_system<AISystem>();
///
/// // Built-in render systems — draw order determined by z_order values.
/// world.add_system<TileMapRenderSystem>();  // typically z_order 0.0
/// world.add_system<SpriteRenderSystem>();   // typically z_order 1.0+
///
/// // Your HUD / overlay render systems last (drawn on top of everything).
/// world.add_system<HUDSystem>();
/// @endcode
///
/// ## z_order and draw layering
///
/// Both systems sort their draw calls by `z_order` (ascending). Lower values
/// are drawn first (behind). Use consistent ranges across your project:
///
/// | z_order range | Suggested use |
/// |---|---|
/// | 0.0 – 1.0     | Background tilemaps (floor, ceiling) |
/// | 1.0 – 5.0     | Object tilemaps (walls, furniture) |
/// | 5.0 – 10.0    | Ground-level sprites (items, effects) |
/// | 10.0 – 20.0   | Character sprites (monsters, player) |
/// | 50.0+         | HUD and UI overlays |
#pragma once

#include <xebble/ecs.hpp>
#include <xebble/renderer.hpp>
#include <xebble/system.hpp>

#include <cstdint>
#include <vector>

namespace xebble {

class Texture;

/// @brief Renders all entities that have a `TileMapLayer` component.
///
/// Each frame, iterates every entity with a `TileMapLayer` and submits its
/// tilemap to the renderer, applying the global `Camera` resource offset so
/// the tilemap scrolls with the camera.
///
/// If an entity also has a `Position` component its position is added to the
/// tile map's own offset (in addition to the camera). This is usually zero —
/// most tilemaps sit at world origin — but non-zero values enable attaching
/// a tilemap to a moving entity.
///
/// **Required components:** `TileMapLayer`
/// **Optional components:** none (Camera resource is used automatically)
///
/// @code
/// // A static dungeon floor and wall tilemap.
/// auto map = std::make_shared<TileMap>(dungeon_sheet, 80, 50, 2);
/// populate_map(*map, dungeon_layout);
///
/// world.build_entity()
///     .with(TileMapLayer{map, /*z_order=*/0.0f})
///     .build();
///
/// // Add the system — it will render the map automatically.
/// world.add_system<TileMapRenderSystem>();
/// @endcode
class TileMapRenderSystem : public System {
public:
    /// @brief Submit all TileMapLayer entities to the renderer.
    void draw(World& world, Renderer& renderer) override;
};

/// @brief Renders all entities that have both a `Position` and a `Sprite` component.
///
/// Each frame, iterates every entity with Position + Sprite, builds a
/// `SpriteInstance` from the sprite's tile UV, tint, and z_order, and submits
/// it to the renderer. The `Camera` resource offset is subtracted so sprites
/// scroll with the world.
///
/// Sprites with the same texture are automatically batched into a single draw
/// call by the renderer for efficiency.
///
/// **Required components:** `Position`, `Sprite`
/// **Optional components:** none (Camera resource is used automatically)
///
/// @code
/// const SpriteSheet& chars = assets.get<SpriteSheet>("characters");
///
/// // Hero entity — will be drawn automatically each frame.
/// world.build_entity()
///     .with(Position{160.0f, 90.0f})
///     .with(Sprite{&chars, TILE_HERO, /*z_order=*/10.0f})
///     .build();
///
/// // Goblin entity.
/// world.build_entity()
///     .with(Position{200.0f, 110.0f})
///     .with(Sprite{&chars, TILE_GOBLIN, /*z_order=*/10.0f, {200, 255, 200, 255}})
///     .build();
///
/// world.add_system<SpriteRenderSystem>();
/// @endcode
class SpriteRenderSystem : public System {
public:
    /// @brief Submit all Position+Sprite entities to the renderer.
    void draw(World& world, Renderer& renderer) override;

private:
    /// Batch metadata for submitting a contiguous run of instances.
    struct BatchRun {
        const Texture* texture = nullptr;
        float z_order = 0.0f;
        uint32_t first = 0;
        uint32_t count = 0;
    };

    /// World::generation() at the time the sorted order was last rebuilt.
    uint64_t last_generation_ = UINT64_MAX;

    /// Parallel array: maps each GPU buffer slot to the entity handle so
    /// we can look up Position for position-only patching.
    std::vector<Entity> gpu_entities_;

    /// Batch runs (same (texture, z_order) group).
    std::vector<BatchRun> batch_runs_;

    /// Per frame-slot: true if this slot's buffer needs a full rewrite
    /// (either first use or the other slot was rebuilt since last use).
    bool frame_dirty_[2] = {true, true};

    /// Number of instances last written.
    uint32_t instance_count_ = 0;

    /// Full rebuild: iterate Position+Sprite, compute all fields, sort,
    /// write directly to the mapped GPU buffer.
    void rebuild(World& world, Renderer& renderer, float cam_x, float cam_y, float fvw, float fvh);
};

} // namespace xebble
