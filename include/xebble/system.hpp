/// @file system.hpp
/// @brief Base class for all ECS systems.
///
/// A `System` encapsulates one coherent slice of game logic or rendering.
/// Override whichever of `init()`, `update()`, and `draw()` are relevant —
/// you do not need to implement all three.
///
/// Systems are added to a `World` and run in registration order by
/// `World::tick_update()` and `World::tick_draw()`.
///
/// ## Defining a system
///
/// @code
/// class MovementSystem : public System {
/// public:
///     void update(World& world, float dt) override {
///         world.each<Position, Velocity>([&](Entity, Position& pos, const Velocity& vel) {
///             pos.x += vel.dx * dt;
///             pos.y += vel.dy * dt;
///         });
///     }
/// };
///
/// class PlayerInputSystem : public System {
/// public:
///     void update(World& world, float dt) override {
///         auto& events = world.resource<EventQueue>().events;
///         Entity player = world.resource<PlayerEntity>().entity;
///
///         for (const Event& e : events) {
///             if (e.type == EventType::KeyPress) {
///                 auto& k = e.key();
///                 IVec2 dir{0, 0};
///                 if (k.key == Key::Up)    dir = { 0, -1};
///                 if (k.key == Key::Down)  dir = { 0,  1};
///                 if (k.key == Key::Left)  dir = {-1,  0};
///                 if (k.key == Key::Right) dir = { 1,  0};
///                 if (dir.x || dir.y) attempt_move(world, player, dir);
///             }
///         }
///     }
/// };
///
/// class HUDRenderSystem : public System {
/// public:
///     void draw(World& world, Renderer& renderer) override {
///         auto& player_stats = world.resource<PlayerStats>();
///         // ... build and submit TextBlock instances ...
///     }
/// };
/// @endcode
///
/// ## Registering systems
///
/// @code
/// world.add_system<MovementSystem>();
/// world.add_system<PlayerInputSystem>();
/// world.add_system<TileMapRenderSystem>();  // built-in
/// world.add_system<SpriteRenderSystem>();   // built-in
/// world.add_system<HUDRenderSystem>();
/// @endcode
///
/// ## System ordering
///
/// Systems run in registration order within each phase:
/// - All `update()` methods run in order before any `draw()` methods.
/// - Use `World::prepend_system<T>()` to insert a system before all others
///   (e.g. to ensure input is processed first).
#pragma once

namespace xebble {

class World;
class Renderer;

/// @brief Base class for ECS game logic and rendering systems.
///
/// Override whichever hooks are relevant to your system and leave the rest
/// as the default no-op implementations.
class System {
public:
    virtual ~System() = default;

    /// @brief Called once after all systems have been added and engine resources
    ///        (assets, renderer, camera, etc.) are ready.
    ///
    /// Use `init()` to perform one-time setup that requires the World or
    /// engine resources to be fully initialised — for example, loading a map,
    /// spawning initial entities, or caching entity handles.
    ///
    /// @code
    /// void init(World& world) override {
    ///     // Cache the player entity handle for quick access.
    ///     player_ = world.resource<PlayerEntity>().entity;
    ///
    ///     // Load the dungeon level.
    ///     auto& assets = world.resource<AssetManager>();
    ///     load_level(world, assets, 1);
    /// }
    /// @endcode
    virtual void init(World& world) {}

    /// @brief Called each fixed-timestep tick for game logic updates.
    ///
    /// @p dt is the fixed timestep duration in seconds (set by
    /// `GameConfig::fixed_timestep`, default 1/60 s). Do not use wall-clock
    /// time for physics or AI — always use @p dt.
    ///
    /// @code
    /// void update(World& world, float dt) override {
    ///     // Advance all projectiles.
    ///     world.each<Position, Projectile>([&](Entity e, Position& pos, Projectile& proj) {
    ///         pos.x += proj.vx * dt;
    ///         pos.y += proj.vy * dt;
    ///         if (pos.x < 0 || pos.x > MAP_W) world.destroy(e);
    ///     });
    /// }
    /// @endcode
    virtual void update(World& world, float dt) {}

    /// @brief Called each frame to submit draw calls to the renderer.
    ///
    /// This runs once per rendered frame (which may be more or less frequent
    /// than the fixed update tick). Only issue render commands here — do not
    /// mutate game state. Use @p renderer to submit `SpriteInstance` batches,
    /// text blocks, or custom draw calls.
    ///
    /// @code
    /// void draw(World& world, Renderer& renderer) override {
    ///     // Draw a debug overlay showing entity bounding boxes.
    ///     world.each<Position, BBox>([&](Entity, const Position& pos, const BBox& bb) {
    ///         draw_rect_outline(renderer, pos.x + bb.ox, pos.y + bb.oy,
    ///                           bb.w, bb.h, {255, 0, 0, 180});
    ///     });
    /// }
    /// @endcode
    virtual void draw(World& world, Renderer& renderer) {}
};

} // namespace xebble
