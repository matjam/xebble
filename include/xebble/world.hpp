/// @file world.hpp
/// @brief World — the central ECS coordinator owning entities, components,
///        systems, and resources.
///
/// `World` is the main object you interact with during gameplay. It holds:
///
/// - **Entities** — lightweight handles created/destroyed via `create_entity()`
///   and `build_entity()`.
/// - **Components** — per-entity data stored in type-keyed pools. Must be
///   registered before use with `register_component<T>()`.
/// - **Systems** — game logic and rendering objects that run each tick.
/// - **Resources** — singleton values shared across all systems (camera,
///   RNG, asset manager, event queue, etc.).
///
/// ## Minimal setup
///
/// @code
/// World world;
///
/// // 1. Register every component type you will use.
/// world.register_component<Position>();
/// world.register_component<Sprite>();
/// world.register_component<Health>();
/// world.register_component<Velocity>();
///
/// // 2. Add per-game resources.
/// world.add_resource(Camera{0.0f, 0.0f});
/// world.add_resource(Rng(42u));
///
/// // 3. Add systems (input → logic → built-in render → custom HUD render).
/// world.add_system<PlayerInputSystem>();
/// world.add_system<MovementSystem>();
/// world.add_system<TileMapRenderSystem>();
/// world.add_system<SpriteRenderSystem>();
/// world.add_system<HUDSystem>();
///
/// // 4. Pass to run() — it calls init_systems() then drives the game loop.
/// return xebble::run(std::move(world), config);
/// @endcode
#pragma once

#include <xebble/ecs.hpp>
#include <xebble/serial.hpp>
#include <xebble/system.hpp>
#include <xebble/types.hpp>

#include <any>
#include <cassert>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace xebble {

class Renderer;

namespace ecs_detail {
    inline uint32_t next_component_id() {
        static uint32_t counter = 0;
        return counter++;
    }

    template<typename T>
    uint32_t component_id() {
        static uint32_t id = next_component_id();
        return id;
    }

    using ComponentId = uint32_t;
} // namespace ecs_detail

class World;

// ---------------------------------------------------------------------------
// EntityBuilder
// ---------------------------------------------------------------------------

/// @brief Fluent builder for constructing entities with components in one expression.
///
/// Obtained from `World::build_entity()`. Chain `with<T>(value)` calls for
/// each component, then call `build()` to get the entity handle.
///
/// @code
/// const SpriteSheet& sheet = assets.get<SpriteSheet>("characters");
///
/// Entity player = world.build_entity()
///     .with(Position{80.0f, 48.0f})
///     .with(Sprite{&sheet, TILE_PLAYER_IDLE, /*z_order=*/5.0f})
///     .with(Health{30, 30})
///     .with(PlayerTag{})
///     .build();
///
/// world.add_resource(PlayerEntity{player});
/// @endcode
class EntityBuilder {
public:
    explicit EntityBuilder(World& world, Entity entity)
        : world_(world), entity_(entity) {}

    /// @brief Add a component to the entity being built.
    template<typename T>
    EntityBuilder& with(T value);

    /// @brief Finalise the entity and return its handle.
    Entity build() { return entity_; }

private:
    World& world_;
    Entity entity_;
};

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------

/// @brief Central ECS coordinator — owns entities, components, systems, and resources.
class World {
public:

    // -----------------------------------------------------------------------
    // Entities
    // -----------------------------------------------------------------------

    /// @brief Create a bare entity with no components.
    ///
    /// @code
    /// Entity e = world.create_entity();
    /// world.add<Position>(e, {0.0f, 0.0f});
    /// world.add<Sprite>(e, {&sheet, TILE_WALL});
    /// @endcode
    Entity create_entity() { return allocator_.create(); }

    /// @brief Begin building an entity with a fluent component-chaining API.
    ///
    /// @code
    /// Entity goblin = world.build_entity()
    ///     .with(Position{x, y})
    ///     .with(Sprite{&sheet, TILE_GOBLIN})
    ///     .with(Health{5, 5})
    ///     .build();
    /// @endcode
    EntityBuilder build_entity() { return EntityBuilder(*this, create_entity()); }

    /// @brief Queue entity @p e for destruction at the end of the current tick.
    ///
    /// The entity and all its components remain valid until `flush_destroyed()`
    /// is called (automatically by the game loop between ticks). This prevents
    /// iterator invalidation when destroying entities inside `each()` loops.
    ///
    /// @code
    /// world.each<Health>([&](Entity e, Health& h) {
    ///     if (h.hp <= 0) world.destroy(e);  // safe — deferred
    /// });
    /// @endcode
    void destroy(Entity e) { pending_destroy_.push_back(e); }

    /// @brief Return true if entity @p e is alive (not yet destroyed).
    bool alive(Entity e) const { return allocator_.alive(e); }

    // -----------------------------------------------------------------------
    // Components
    // -----------------------------------------------------------------------

    /// @brief Register a component type before adding it to any entity.
    ///
    /// Must be called once per component type before the first `add<T>()`.
    /// Typically done during world setup, before `run()`.
    ///
    /// @code
    /// world.register_component<Position>();
    /// world.register_component<Sprite>();
    /// world.register_component<Health>();
    /// @endcode
    template<typename T>
    void register_component() {
        auto id = ecs_detail::component_id<T>();
        if (id >= pools_.size()) pools_.resize(id + 1);
        pools_[id] = std::make_unique<ComponentPool<T>>();
    }

    /// @brief Register a serializable component type.
    ///
    /// Like `register_component<T>()`, but the component will also be
    /// included in `snapshot()` blobs and restored by `restore()`.
    ///
    /// Requires that `ComponentName<T>` is specialized and that T is
    /// trivially copyable.
    ///
    /// @code
    /// struct Health { int hp; int max_hp; };
    /// template<> struct xebble::ComponentName<Health>
    ///     { static constexpr std::string_view value = "game::Health"; };
    ///
    /// world.register_serializable_component<Health>();
    /// @endcode
    template<typename T>
        requires serial_detail::HasComponentName<T> && std::is_trivially_copyable_v<T>
    void register_serializable_component() {
        auto id = ecs_detail::component_id<T>();
        if (id >= pools_.size()) pools_.resize(id + 1);
        pools_[id] = std::make_unique<SerializableComponentPool<T>>();
    }

    /// @brief Add component @p value to entity @p e and return a reference to it.
    ///
    /// The component type must have been registered with `register_component<T>()`.
    /// Behaviour is undefined if @p e already has this component.
    ///
    /// @code
    /// auto& health = world.add<Health>(e, Health{20, 20});
    /// health.max_hp = 25;  // can modify immediately via the returned reference
    /// @endcode
    template<typename T>
    T& add(Entity e, T value) {
        auto& pool = get_pool<T>();
        pool.add(e, std::move(value));
        return pool.get(e);
    }

    /// @brief Remove the component of type T from entity @p e.
    ///
    /// No-op if @p e does not have the component.
    template<typename T>
    void remove(Entity e) { get_pool<T>().remove(e); }

    /// @brief Return a reference to the component of type T on entity @p e.
    ///
    /// Undefined behaviour if @p e does not have this component.
    /// Use `has<T>(e)` first if unsure.
    ///
    /// @code
    /// world.get<Position>(player).x += speed * dt;
    /// world.get<Health>(target).hp -= damage;
    /// @endcode
    template<typename T>
    T& get(Entity e) { return get_pool<T>().get(e); }

    template<typename T>
    const T& get(Entity e) const { return get_pool<T>().get(e); }

    /// @brief Return true if entity @p e currently has a component of type T.
    ///
    /// @code
    /// if (world.has<Poisoned>(e)) apply_poison_tick(world, e);
    /// @endcode
    template<typename T>
    bool has(Entity e) const {
        auto id = ecs_detail::component_id<T>();
        if (id >= pools_.size() || !pools_[id]) return false;
        return dynamic_cast<const ComponentPool<T>*>(pools_[id].get())->has(e);
    }

    // -----------------------------------------------------------------------
    // Iteration
    // -----------------------------------------------------------------------

    /// @brief Iterate all entities that have component T, calling fn(entity, T&).
    ///
    /// @code
    /// // Apply gravity to every entity with a Velocity component.
    /// world.each<Velocity>([&](Entity, Velocity& v) {
    ///     v.dy += GRAVITY * dt;
    /// });
    /// @endcode
    template<typename T, typename Fn>
    void each(Fn&& fn) {
        auto& pool = get_pool<T>();
        for (size_t i = 0; i < pool.size(); i++)
            fn(pool.dense_entity(i), pool.dense_component(i));
    }

    /// @brief Iterate all entities that have T1, T2, and all additional types.
    ///
    /// Iterates the pool of T1 and skips entities missing any of the other
    /// required components.
    ///
    /// @code
    /// // Move all entities that have both a Position and a Velocity.
    /// world.each<Position, Velocity>([&](Entity, Position& pos, Velocity& vel) {
    ///     pos.x += vel.dx * dt;
    ///     pos.y += vel.dy * dt;
    /// });
    ///
    /// // Three-component query: only entities with all three.
    /// world.each<Position, Sprite, Blink>([&](Entity e,
    ///         Position&, Sprite& spr, Blink& blink) {
    ///     blink.timer -= dt;
    ///     spr.tint.a = blink.visible ? 255 : 0;
    ///     if (blink.timer <= 0) world.remove<Blink>(e);
    /// });
    /// @endcode
    template<typename T1, typename T2, typename... Rest, typename Fn>
    void each(Fn&& fn) {
        auto& pool = get_pool<T1>();
        for (size_t i = 0; i < pool.size(); i++) {
            Entity e = pool.dense_entity(i);
            if (has<T2>(e) && (has<Rest>(e) && ...))
                fn(e, pool.dense_component(i), get<T2>(e), get<Rest>(e)...);
        }
    }

    // -----------------------------------------------------------------------
    // Resources
    // -----------------------------------------------------------------------

    /// @brief Store a singleton value accessible to all systems.
    ///
    /// Resources are keyed by type — there is exactly one instance of each
    /// resource type. Call `add_resource` once per type during setup.
    ///
    /// @code
    /// world.add_resource(Camera{0.0f, 0.0f});
    /// world.add_resource(Rng(world_seed));
    /// world.add_resource(GameState{GameState::Playing});
    /// @endcode
    template<typename T>
    void add_resource(T value) {
        auto id = ecs_detail::component_id<T>();
        resources_[id] = std::move(value);
    }

    /// @brief Store a serializable singleton value accessible to all systems.
    ///
    /// Like `add_resource<T>()`, but the resource will also be included in
    /// `snapshot()` blobs and restored by `restore()`.
    ///
    /// Requires that `ResourceName<T>` is specialized and that T is
    /// trivially copyable.
    ///
    /// @code
    /// struct WorldSeed { uint64_t value; };
    /// template<> struct xebble::ResourceName<WorldSeed>
    ///     { static constexpr std::string_view value = "game::WorldSeed"; };
    ///
    /// world.add_serializable_resource(WorldSeed{42u});
    /// @endcode
    template<typename T>
        requires serial_detail::HasResourceName<T> && std::is_trivially_copyable_v<T>
    void add_serializable_resource(T value) {
        auto id = ecs_detail::component_id<T>();
        resources_[id] = std::move(value);

        serial_detail::ResourceSerializer ser;
        ser.name = std::string(ResourceName<T>::value);
        ser.write = [](const std::any& a, std::vector<uint8_t>& out) {
            const T& v = std::any_cast<const T&>(a);
            serial_detail::append(out, v);
        };
        ser.read = [](std::any& a, const uint8_t* data, size_t size) {
            if (size < sizeof(T)) return;
            T v{};
            std::memcpy(&v, data, sizeof(T));
            a = v;
        };
        resource_serializers_[id] = std::move(ser);
    }

    /// @brief Remove a resource by type. No-op if absent.
    template<typename T>
    void remove_resource() { resources_.erase(ecs_detail::component_id<T>()); }

    /// @brief Return a reference to the resource of type T.
    ///
    /// Throws `std::out_of_range` if the resource has not been added.
    ///
    /// @code
    /// auto& cam  = world.resource<Camera>();
    /// auto& rng  = world.resource<Rng>();
    /// auto& evts = world.resource<EventQueue>().events;
    /// @endcode
    template<typename T>
    T& resource() {
        return std::any_cast<T&>(resources_.at(ecs_detail::component_id<T>()));
    }

    template<typename T>
    const T& resource() const {
        return std::any_cast<const T&>(resources_.at(ecs_detail::component_id<T>()));
    }

    /// @brief Return true if a resource of type T has been added.
    template<typename T>
    bool has_resource() const {
        return resources_.contains(ecs_detail::component_id<T>());
    }

    // -----------------------------------------------------------------------
    // Systems
    // -----------------------------------------------------------------------

    /// @brief Append a system of type T (constructed with args) to the end of the list.
    ///
    /// Systems run in registration order. Append rendering systems after logic
    /// systems to ensure the draw commands see the latest game state.
    ///
    /// @code
    /// world.add_system<PlayerInputSystem>();
    /// world.add_system<AISystem>();
    /// world.add_system<PhysicsSystem>();
    /// world.add_system<TileMapRenderSystem>();
    /// world.add_system<SpriteRenderSystem>();
    /// world.add_system<HUDSystem>();
    /// @endcode
    template<typename T, typename... Args>
    void add_system(Args&&... args) {
        systems_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
    }

    /// @brief Prepend a system of type T before all currently registered systems.
    ///
    /// Use this to ensure a system always runs first — for example, an input
    /// dispatcher that populates an `EventQueue` resource before other systems
    /// consume it.
    template<typename T, typename... Args>
    void prepend_system(Args&&... args) {
        systems_.insert(systems_.begin(), std::make_unique<T>(std::forward<Args>(args)...));
    }

    // -----------------------------------------------------------------------
    // Game loop hooks (called by run())
    // -----------------------------------------------------------------------

    /// @brief Call `init()` on every system. Called once before the main loop.
    void init_systems();

    /// @brief Call `update(dt)` on every system. Called at fixed timestep.
    void tick_update(float dt);

    /// @brief Call `draw(renderer)` on every system. Called once per rendered frame.
    void tick_draw(Renderer& renderer);

    /// @brief Destroy all entities queued via `destroy()`. Called between ticks.
    void flush_destroyed();

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------

    /// @brief Snapshot all serializable entities, components, and resources
    ///        into a self-contained binary blob.
    ///
    /// Only components registered via `register_serializable_component<T>()`
    /// and resources registered via `add_serializable_resource<T>()` are
    /// included. Everything else (engine resources, non-serializable pools) is
    /// silently skipped.
    ///
    /// The blob is independent of the current process — it can be written to
    /// disk and loaded by a fresh instance of the game.
    ///
    /// @code
    /// auto blob = world.snapshot();
    /// // Write blob to disk…
    /// std::ofstream f("save.bin", std::ios::binary);
    /// f.write(reinterpret_cast<const char*>(blob.data()), blob.size());
    /// @endcode
    std::vector<uint8_t> snapshot() const;

    /// @brief Restore world state from a blob produced by `snapshot()`.
    ///
    /// All entities in the World are destroyed first. Serializable components
    /// are re-created for the restored entities. Non-serializable components
    /// and engine resources are left untouched.
    ///
    /// Returns an error if the blob is corrupt or has an incompatible format
    /// version.
    ///
    /// @code
    /// auto blob = load_from_disk("save.bin");
    /// if (auto r = world.restore(blob); !r)
    ///     log_error(r.error().message);
    /// @endcode
    std::expected<void, Error> restore(std::span<const uint8_t> blob);

private:
    template<typename T>
    ComponentPool<T>& get_pool() {
        auto id = ecs_detail::component_id<T>();
        assert(id < pools_.size() && pools_[id] && "Component type not registered");
        return dynamic_cast<ComponentPool<T>&>(*pools_[id]);
    }

    template<typename T>
    const ComponentPool<T>& get_pool() const {
        auto id = ecs_detail::component_id<T>();
        assert(id < pools_.size() && pools_[id] && "Component type not registered");
        return dynamic_cast<const ComponentPool<T>&>(*pools_[id]);
    }

    EntityAllocator allocator_;
    std::vector<std::unique_ptr<IComponentPool>> pools_;
    std::unordered_map<ecs_detail::ComponentId, std::any> resources_;
    std::unordered_map<ecs_detail::ComponentId, serial_detail::ResourceSerializer> resource_serializers_;
    std::vector<std::unique_ptr<System>> systems_;
    std::vector<Entity> pending_destroy_;
};

// EntityBuilder::with implementation (needs World to be complete).
template<typename T>
EntityBuilder& EntityBuilder::with(T value) {
    world_.add<T>(entity_, std::move(value));
    return *this;
}

} // namespace xebble
