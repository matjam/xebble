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
    explicit EntityBuilder(World& world, Entity entity) : world_(world), entity_(entity) {}

    /// @brief Add a component to the entity being built.
    template<typename T>
    EntityBuilder& with(T value);

    /// @brief Finalise the entity and return its handle.
    [[nodiscard]] Entity build() { return entity_; }

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
    [[nodiscard]] Entity create_entity() {
        Entity e = allocator_.create();
        ensure_mask_slot(ecs_detail::entity_index(e));
        return e;
    }

    /// @brief Begin building an entity with a fluent component-chaining API.
    ///
    /// @code
    /// Entity goblin = world.build_entity()
    ///     .with(Position{x, y})
    ///     .with(Sprite{&sheet, TILE_GOBLIN})
    ///     .with(Health{5, 5})
    ///     .build();
    /// @endcode
    [[nodiscard]] EntityBuilder build_entity() { return EntityBuilder(*this, create_entity()); }

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
    [[nodiscard]] bool alive(Entity e) const { return allocator_.alive(e); }

    /// @brief Monotonically increasing counter bumped whenever the set of
    ///        entities or their components changes (add, remove, destroy, restore).
    ///        Systems can cache this value and skip work when it hasn't changed.
    [[nodiscard]] uint64_t generation() const { return generation_; }

    /// @brief Manually bump the generation counter to signal that component
    ///        data (not structure) has changed — e.g. after modifying Position
    ///        or Sprite values.  Systems that cache derived data (like
    ///        `SpriteRenderSystem`) will detect this and rebuild next frame.
    void mark_changed() { ++generation_; }

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
        if (id >= pools_.size())
            pools_.resize(id + 1);
        pools_[id] = std::make_unique<ComponentPool<T>>();
        grow_mask_width(id);
    }

    /// @brief Register a serializable component type.
    ///
    /// Like `register_component<T>()`, but the component will also be
    /// included in `snapshot()` blobs and restored by `restore()`.
    ///
    /// Requires that `ComponentName<T>` is specialized.
    ///
    /// **Trivially-copyable** types are serialized automatically via memcpy.
    ///
    /// **Non-trivially-copyable** types must provide custom hooks:
    /// - `void serialize(BinaryWriter&) const`
    /// - `static T deserialize(BinaryReader&)`
    ///
    /// @code
    /// // Trivially-copyable — automatic.
    /// struct Health { int hp; int max_hp; };
    /// template<> struct xebble::ComponentName<Health>
    ///     { static constexpr std::string_view value = "game::Health"; };
    ///
    /// // Non-trivially-copyable — custom hooks.
    /// struct Inventory {
    ///     std::vector<std::string> items;
    ///     void serialize(xebble::BinaryWriter& w) const { /* ... */ }
    ///     static Inventory deserialize(xebble::BinaryReader& r) { /* ... */ }
    /// };
    /// template<> struct xebble::ComponentName<Inventory>
    ///     { static constexpr std::string_view value = "game::Inventory"; };
    ///
    /// world.register_serializable_component<Health>();
    /// world.register_serializable_component<Inventory>();
    /// @endcode
    template<typename T>
        requires serial_detail::HasComponentName<T>
    void register_serializable_component() {
        static_assert(std::is_trivially_copyable_v<T> || serial_detail::CustomSerializable<T>,
                      "Serializable component must be either trivially copyable or provide "
                      "serialize(BinaryWriter&) const and static deserialize(BinaryReader&) hooks");

        auto id = ecs_detail::component_id<T>();
        if (id >= pools_.size())
            pools_.resize(id + 1);

        if constexpr (std::is_trivially_copyable_v<T>) {
            pools_[id] = std::make_unique<SerializableComponentPool<T>>();
        } else {
            pools_[id] = std::make_unique<CustomSerializableComponentPool<T>>();
        }
        grow_mask_width(id);
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
        set_mask_bit(ecs_detail::entity_index(e), ecs_detail::component_id<T>());
        ++generation_;
        return pool.get(e);
    }

    /// @brief Remove the component of type T from entity @p e.
    ///
    /// No-op if @p e does not have the component.
    template<typename T>
    void remove(Entity e) {
        get_pool<T>().remove(e);
        clear_mask_bit(ecs_detail::entity_index(e), ecs_detail::component_id<T>());
        ++generation_;
    }

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
    [[nodiscard]] T& get(Entity e) {
        return get_pool<T>().get(e);
    }

    template<typename T>
    [[nodiscard]] const T& get(Entity e) const {
        return get_pool<T>().get(e);
    }

    /// @brief Return true if entity @p e currently has a component of type T.
    ///
    /// @code
    /// if (world.has<Poisoned>(e)) apply_poison_tick(world, e);
    /// @endcode
    template<typename T>
    [[nodiscard]] bool has(Entity e) const {
        return test_mask_bit(ecs_detail::entity_index(e), ecs_detail::component_id<T>());
    }

    /// @brief Return a typed reference to the component pool for T.
    ///
    /// Provides direct access to the dense storage for advanced use cases
    /// such as bulk iteration or direct memory access to contiguous data.
    /// The component type must have been registered.
    ///
    /// @code
    /// auto& pool = world.pool<Position>();
    /// for (size_t i = 0; i < pool.size(); ++i)
    ///     process(pool.dense_entity(i), pool.dense_component(i));
    /// @endcode
    template<typename T>
    [[nodiscard]] ComponentPool<T>& pool() {
        return get_pool<T>();
    }

    template<typename T>
    [[nodiscard]] const ComponentPool<T>& pool() const {
        return get_pool<T>();
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
        auto& pool1 = get_pool<T1>();
        // Cache typed pool references for get() calls (one dynamic_cast each,
        // done once here instead of per-entity).
        auto& pool2 = get_pool<T2>();
        [[maybe_unused]] auto typed_rest = std::tuple<ComponentPool<Rest>&...>(get_pool<Rest>()...);

        // Build a required-component bitmask once for all filter types.
        // The T1 pool is iterated directly so we only need bits for T2..Rest.
        auto required = make_component_mask(ecs_detail::component_id<T2>(),
                                            ecs_detail::component_id<Rest>()...);

        for (size_t i = 0; i < pool1.size(); i++) {
            Entity e = pool1.dense_entity(i);
            if (test_mask(ecs_detail::entity_index(e), required))
                fn(e, pool1.dense_component(i), pool2.get(e),
                   std::get<ComponentPool<Rest>&>(typed_rest).get(e)...);
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
    /// Requires that `ResourceName<T>` is specialized. Trivially-copyable
    /// types are serialized automatically; non-trivially-copyable types must
    /// provide `serialize(BinaryWriter&) const` and
    /// `static T deserialize(BinaryReader&)` hooks.
    ///
    /// @code
    /// struct WorldSeed { uint64_t value; };
    /// template<> struct xebble::ResourceName<WorldSeed>
    ///     { static constexpr std::string_view value = "game::WorldSeed"; };
    ///
    /// world.add_serializable_resource(WorldSeed{42u});
    /// @endcode
    template<typename T>
        requires serial_detail::HasResourceName<T>
    void add_serializable_resource(T value) {
        static_assert(std::is_trivially_copyable_v<T> || serial_detail::CustomSerializable<T>,
                      "Serializable resource must be either trivially copyable or provide "
                      "serialize(BinaryWriter&) const and static deserialize(BinaryReader&) hooks");

        auto id = ecs_detail::component_id<T>();
        resources_[id] = std::move(value);

        serial_detail::ResourceSerializer ser;
        ser.name = std::string(ResourceName<T>::value);

        if constexpr (std::is_trivially_copyable_v<T>) {
            ser.write = [](const std::any& a, std::vector<uint8_t>& out) {
                const T& v = std::any_cast<const T&>(a);
                serial_detail::append(out, v);
            };
            ser.read = [](std::any& a, const uint8_t* data, size_t size) {
                if (size < sizeof(T))
                    return;
                T v{};
                std::memcpy(&v, data, sizeof(T));
                a = v;
            };
        } else {
            ser.write = [](const std::any& a, std::vector<uint8_t>& out) {
                const T& v = std::any_cast<const T&>(a);
                BinaryWriter writer(out);
                v.serialize(writer);
            };
            ser.read = [](std::any& a, const uint8_t* data, size_t size) {
                BinaryReader reader(data, size);
                a = T::deserialize(reader);
            };
        }
        resource_serializers_[id] = std::move(ser);
    }

    /// @brief Remove a resource by type. No-op if absent.
    template<typename T>
    void remove_resource() {
        resources_.erase(ecs_detail::component_id<T>());
    }

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
    [[nodiscard]] T& resource() {
        return std::any_cast<T&>(resources_.at(ecs_detail::component_id<T>()));
    }

    template<typename T>
    [[nodiscard]] const T& resource() const {
        return std::any_cast<const T&>(resources_.at(ecs_detail::component_id<T>()));
    }

    /// @brief Return true if a resource of type T has been added.
    template<typename T>
    [[nodiscard]] bool has_resource() const {
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
    [[nodiscard]] std::vector<uint8_t> snapshot() const;

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
    [[nodiscard]] std::expected<void, Error> restore(std::span<const uint8_t> blob);

private:
    template<typename T>
    ComponentPool<T>& get_pool() {
        auto id = ecs_detail::component_id<T>();
        assert(id < pools_.size() && pools_[id] && "Component type not registered");
        // dynamic_cast is required because ComponentPool<T> uses virtual
        // inheritance from IComponentPool.  This is called once per query
        // (outside the loop), not per entity, so the cost is negligible.
        return dynamic_cast<ComponentPool<T>&>(*pools_[id]);
    }

    template<typename T>
    const ComponentPool<T>& get_pool() const {
        auto id = ecs_detail::component_id<T>();
        assert(id < pools_.size() && pools_[id] && "Component type not registered");
        return dynamic_cast<const ComponentPool<T>&>(*pools_[id]);
    }

    /// @brief Fast pool lookup for has() checks — returns the type-erased
    /// pointer without any cast.  O(1), no RTTI.
    IComponentPool* get_pool_raw(ecs_detail::ComponentId id) const {
        if (id >= pools_.size() || !pools_[id])
            return nullptr;
        return pools_[id].get();
    }

    // -----------------------------------------------------------------------
    // Component bitmask — flat array of uint64_t words, `mask_words_` words
    // per entity slot.  Bit `component_id` in entity slot `idx` is at:
    //   component_masks_[idx * mask_words_ + (component_id / 64)]
    //       & (1ULL << (component_id % 64))
    // -----------------------------------------------------------------------

    /// Ensure the mask array has room for entity slot `idx`.
    void ensure_mask_slot(uint32_t idx) {
        size_t needed = static_cast<size_t>(idx + 1) * mask_words_;
        if (needed > component_masks_.size())
            component_masks_.resize(needed, 0);
    }

    /// Grow mask width when a new component ID exceeds the current word count.
    void grow_mask_width(uint32_t component_id) {
        uint32_t words_needed = component_id / 64 + 1;
        if (words_needed <= mask_words_)
            return;

        // Must widen every existing entity's mask.  Re-stride the flat array
        // from mask_words_ → words_needed.
        uint32_t old_words = mask_words_;
        uint32_t new_words = words_needed;
        uint32_t slot_count =
            (old_words > 0) ? static_cast<uint32_t>(component_masks_.size() / old_words) : 0;

        std::vector<uint64_t> grown(static_cast<size_t>(slot_count) * new_words, 0);
        for (uint32_t s = 0; s < slot_count; ++s) {
            for (uint32_t w = 0; w < old_words; ++w)
                grown[static_cast<size_t>(s) * new_words + w] =
                    component_masks_[static_cast<size_t>(s) * old_words + w];
        }
        component_masks_ = std::move(grown);
        mask_words_ = new_words;
    }

    void set_mask_bit(uint32_t slot, uint32_t component_id) {
        size_t off = static_cast<size_t>(slot) * mask_words_ + (component_id / 64);
        component_masks_[off] |= (1ULL << (component_id % 64));
    }

    void clear_mask_bit(uint32_t slot, uint32_t component_id) {
        size_t off = static_cast<size_t>(slot) * mask_words_ + (component_id / 64);
        component_masks_[off] &= ~(1ULL << (component_id % 64));
    }

    bool test_mask_bit(uint32_t slot, uint32_t component_id) const {
        size_t off = static_cast<size_t>(slot) * mask_words_ + (component_id / 64);
        if (off >= component_masks_.size())
            return false;
        return (component_masks_[off] & (1ULL << (component_id % 64))) != 0;
    }

    /// Clear all mask bits for a given entity slot.
    void clear_mask(uint32_t slot) {
        size_t base = static_cast<size_t>(slot) * mask_words_;
        for (uint32_t w = 0; w < mask_words_; ++w)
            component_masks_[base + w] = 0;
    }

    /// A small inline mask used as a query key — same word width as the
    /// per-entity masks.
    using MaskVec = std::vector<uint64_t>;

    /// Build a required-component mask from a list of component IDs.
    template<typename... Ids>
    MaskVec make_component_mask(Ids... ids) const {
        MaskVec mask(mask_words_, 0);
        ((mask[ids / 64] |= (1ULL << (ids % 64))), ...);
        return mask;
    }

    /// Test whether entity slot `idx` has ALL bits in `required` set.
    bool test_mask(uint32_t idx, const MaskVec& required) const {
        size_t base = static_cast<size_t>(idx) * mask_words_;
        for (uint32_t w = 0; w < mask_words_; ++w)
            if ((component_masks_[base + w] & required[w]) != required[w])
                return false;
        return true;
    }

    EntityAllocator allocator_;
    std::vector<std::unique_ptr<IComponentPool>> pools_;
    std::vector<uint64_t> component_masks_; ///< Flat bitmask array, mask_words_ per entity slot.
    uint32_t mask_words_ = 1;               ///< Words per entity (grows in 64-bit increments).
    uint64_t generation_ = 0; ///< Bumped on structural changes (add/remove/destroy/restore).
    std::unordered_map<ecs_detail::ComponentId, std::any> resources_;
    std::unordered_map<ecs_detail::ComponentId, serial_detail::ResourceSerializer>
        resource_serializers_;
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
