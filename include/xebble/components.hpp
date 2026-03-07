/// @file components.hpp
/// @brief Built-in ECS components and resources provided by Xebble.
///
/// These components cover the most common per-entity rendering needs. Attach
/// them to entities with `World::add()` or `EntityBuilder::with()`, and the
/// built-in rendering systems (`SpriteRenderSystem`, `TileMapRenderSystem`)
/// will handle drawing automatically.
///
/// ## Overview
///
/// | Component / Resource | Purpose |
/// |---|---|
/// | `Position`     | World-space position for sprites and other entities. |
/// | `Sprite`       | Draws one tile from a spritesheet at an entity's position. |
/// | `TileMapLayer` | Attaches a tilemap to an entity so it gets rendered. |
/// | `Camera`       | Resource: viewport scroll offset in world pixels. |
///
/// ## Typical setup
///
/// @code
/// // Register all built-in component types before adding entities.
/// world.register_component<Position>();
/// world.register_component<Sprite>();
/// world.register_component<TileMapLayer>();
///
/// // Add the camera resource (top-left world position of the viewport).
/// world.add_resource(Camera{0.0f, 0.0f});
///
/// // Add the built-in rendering systems.
/// world.add_system<TileMapRenderSystem>();
/// world.add_system<SpriteRenderSystem>();
/// @endcode
#pragma once

#include <xebble/types.hpp>
#include <xebble/tilemap.hpp>
#include <xebble/serial.hpp>

#include <memory>

namespace xebble {

class SpriteSheet;

// ---------------------------------------------------------------------------
// Position
// ---------------------------------------------------------------------------

/// @brief World-space position of an entity in virtual pixels.
///
/// (0, 0) is the top-left of the world. x increases rightward, y increases
/// downward. This component is used by `SpriteRenderSystem` to place sprites
/// on screen relative to the `Camera` resource.
///
/// @code
/// // Place the player sprite at tile (5, 3) on a 16-pixel tile grid.
/// world.build_entity()
///     .with(Position{5 * 16.0f, 3 * 16.0f})
///     .with(Sprite{&sheet, TILE_PLAYER})
///     .build();
///
/// // Move an entity each update tick.
/// void update(World& world, float dt) override {
///     world.each<Position, Velocity>([&](Entity e, Position& pos, Velocity& vel) {
///         pos.x += vel.dx * dt;
///         pos.y += vel.dy * dt;
///     });
/// }
/// @endcode
struct Position {
    float x = 0.0f;  ///< Horizontal world position in virtual pixels.
    float y = 0.0f;  ///< Vertical   world position in virtual pixels.
};

// ---------------------------------------------------------------------------
// Sprite
// ---------------------------------------------------------------------------

/// @brief Renders a single tile from a spritesheet at the entity's `Position`.
///
/// The `SpriteRenderSystem` draws all entities that have both a `Position` and
/// a `Sprite`, sorted by `z_order` (lower = drawn first / behind).
///
/// The `tint` colour is multiplied with the texture colour: `{255, 255, 255, 255}`
/// (the default) passes the texture through unchanged. Set it to a different
/// colour to tint the sprite — useful for hit flashes, poisoned states,
/// selection highlights, etc.
///
/// @code
/// const SpriteSheet& sheet = assets.get<SpriteSheet>("characters");
///
/// // A goblin entity with a red health indicator tint.
/// world.build_entity()
///     .with(Position{100.0f, 80.0f})
///     .with(Sprite{&sheet, TILE_GOBLIN, /*z_order=*/1.0f, {255, 120, 120, 255}})
///     .build();
///
/// // Change the sprite tile when an entity equips armour.
/// world.get<Sprite>(entity).tile_index = TILE_KNIGHT;
///
/// // Flash a sprite white for one frame when it takes damage.
/// world.get<Sprite>(entity).tint = {255, 255, 255, 255};
/// schedule_restore_tint(entity, original_tint, /*delay=*/0.1f);
///
/// // Draw the player above all other sprites.
/// world.get<Sprite>(player).z_order = 10.0f;
/// @endcode
struct Sprite {
    const SpriteSheet* sheet      = nullptr;              ///< Atlas to sample from.
    uint32_t           tile_index = 0;                    ///< Row-major tile index in the atlas.
    float              z_order    = 0.0f;                 ///< Draw order (lower = behind).
    Color              tint       = {255, 255, 255, 255}; ///< Multiplicative colour tint.
    float              scale      = 1.0f;                 ///< Uniform scale multiplier (1 = native size).
    float              rotation   = 0.0f;                 ///< Rotation in radians, counter-clockwise.
    float              pivot_x    = 0.5f;                 ///< Rotation pivot X in 0–1 quad-local space (0=left, 1=right).
    float              pivot_y    = 0.5f;                 ///< Rotation pivot Y in 0–1 quad-local space (0=top, 1=bottom).
};

// ---------------------------------------------------------------------------
// TileMapLayer
// ---------------------------------------------------------------------------

/// @brief Attaches a `TileMap` to an entity for rendering by `TileMapRenderSystem`.
///
/// Use a `shared_ptr` so the same tilemap can be referenced from both the ECS
/// component and your game-logic code without ownership issues.
///
/// `TileMapRenderSystem` draws all `TileMapLayer` entities, using `z_order` to
/// sort them relative to sprites and other layers (lower = behind).
///
/// @code
/// // Create the map once and share ownership.
/// auto map = std::make_shared<TileMap>(sheet, 80, 50, 2);
/// populate_floor_layer(*map, dungeon);
/// populate_wall_layer(*map, dungeon);
///
/// // Attach to the world.
/// world.build_entity()
///     .with(TileMapLayer{map, /*z_order=*/0.0f})
///     .build();
///
/// // Keep a raw pointer for game-logic writes (safe: world owns the entity,
/// // which shares ownership of the map via the shared_ptr).
/// TileMap* game_map = map.get();
///
/// // Update a cell when the player breaks a wall.
/// game_map->set_tile(1, wall_x, wall_y, TILE_RUBBLE);
/// @endcode
struct TileMapLayer {
    std::shared_ptr<TileMap> tilemap;         ///< The tilemap to render.
    float                    z_order = 0.0f;  ///< Draw order (lower = behind sprites).
};

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

/// @brief World resource: the top-left corner of the viewport in world pixels.
///
/// `SpriteRenderSystem` and `TileMapRenderSystem` both subtract the camera
/// offset from entity positions before drawing, producing a scrolling world.
///
/// Store this as a World resource (not a component on an entity) so all
/// rendering systems can access it uniformly.
///
/// @code
/// // Initialise the camera when setting up the world.
/// world.add_resource(Camera{0.0f, 0.0f});
///
/// // Centre the camera on the player each frame.
/// void update(World& world, float dt) override {
///     auto& cam = world.resource<Camera>();
///     Position& p = world.get<Position>(player_entity);
///
///     // Keep the player centred on a 320×200 virtual screen.
///     cam.x = p.x - 160.0f;
///     cam.y = p.y - 100.0f;
///
///     // Clamp to the map boundaries (map is 80×50 tiles, tile size 16).
///     cam.x = std::clamp(cam.x, 0.0f, 80.0f * 16.0f - 320.0f);
///     cam.y = std::clamp(cam.y, 0.0f, 50.0f * 16.0f - 200.0f);
/// }
/// @endcode
struct Camera {
    float x = 0.0f;  ///< Left edge of the viewport in world pixels.
    float y = 0.0f;  ///< Top  edge of the viewport in world pixels.
};

} // namespace xebble

// ---------------------------------------------------------------------------
// Serialization opt-in for built-in serializable components
// ---------------------------------------------------------------------------

/// @brief Opt `Position` into World serialization.
template<> struct xebble::ComponentName<xebble::Position>
    { static constexpr std::string_view value = "xebble::Position"; };

/// @brief Opt `Camera` into World resource serialization.
///
/// Camera is stored as a resource.  Specializing `ResourceName` here (rather
/// than `ComponentName`) lets it be saved and restored via
/// `add_serializable_resource<Camera>()` / `World::snapshot()`.
template<> struct xebble::ResourceName<xebble::Camera>
    { static constexpr std::string_view value = "xebble::Camera"; };
