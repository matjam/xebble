/// @file ecs.hpp
/// @brief ECS internals: Entity handle, generational allocator, and component storage.
///
/// This header defines the low-level building blocks of the ECS. Most game
/// code uses the higher-level `World` API in `world.hpp` and never touches
/// these types directly. They are documented here for completeness and for
/// developers extending the ECS.
///
/// ## Entity handles
///
/// Entities are opaque handles combining a 20-bit index and a 12-bit
/// generation counter packed into a single `uint32_t`. The generation counter
/// allows the allocator to detect use-after-destroy bugs: a handle whose
/// generation no longer matches the slot's current generation is considered
/// dead.
///
/// @code
/// Entity e = world.create_entity();
/// world.add<Position>(e, {10.0f, 20.0f});
/// world.destroy(e);  // queues for deferred removal
///
/// // After flush_destroyed():
/// world.alive(e);  // returns false — generation mismatch
/// @endcode
///
/// ## Component storage
///
/// Components are stored in `ComponentPool<T>` — a sparse-set that gives
/// O(1) add, remove, and lookup, and contiguous dense iteration. The sparse
/// set uses two parallel arrays: a sparse index array (indexed by entity slot)
/// pointing into the dense arrays, which store the actual entity handles and
/// component values in packed order.
///
/// This layout is cache-friendly for `World::each<T>()` loops because all
/// components of a given type are contiguous in memory, maximising data
/// throughput regardless of how many entities exist total.
#pragma once

#include <climits>
#include <cstdint>
#include <span>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// Entity
// ---------------------------------------------------------------------------

/// @brief Opaque entity handle with embedded generational index.
///
/// Treat this as an opaque ID. The internal bit layout is an implementation
/// detail — use `World::alive()` to check validity rather than inspecting
/// the `id` field directly.
///
/// Entities are cheap to copy and compare (they are just a `uint32_t`).
///
/// @code
/// Entity player = world.create_entity();
/// Entity enemy  = world.build_entity()
///                     .with(Position{50.0f, 30.0f})
///                     .with(Sprite{&sheet, TILE_GOBLIN})
///                     .build();
///
/// // Store entity handles in resources or your own data structures.
/// world.add_resource(PlayerEntity{player});
///
/// // Check whether an entity is still alive before using it.
/// if (world.alive(target_entity)) {
///     world.get<Health>(target_entity).hp -= damage;
/// }
/// @endcode
struct Entity {
    uint32_t id = 0; ///< Packed index + generation. Treat as opaque.
    bool operator==(Entity other) const { return id == other.id; }
    bool operator!=(Entity other) const { return id != other.id; }
};

class Event;

/// @brief World resource holding all input events for the current frame.
///
/// Populated each frame by `run()` before `tick_update()` is called. Iterate
/// over `events` in your systems to handle keyboard, mouse, and window input.
///
/// @code
/// void update(World& world, float dt) override {
///     for (const Event& e : world.resource<EventQueue>().events) {
///         if (e.type == EventType::KeyPress && e.key().key == Key::Escape)
///             world.resource<AppState>().running = false;
///     }
/// }
/// @endcode
struct EventQueue {
    std::vector<Event> events; ///< Events collected during the current frame's poll.
};

// ---------------------------------------------------------------------------
// Internal implementation details
// ---------------------------------------------------------------------------
// The types below are implementation details of the ECS. They are public
// because templates require it, but game code should not use them directly.

namespace ecs_detail {
constexpr uint32_t INDEX_BITS = 20;
constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1;
constexpr uint32_t GEN_SHIFT = INDEX_BITS;

inline uint32_t entity_index(Entity e) {
    return e.id & INDEX_MASK;
}
inline uint32_t entity_gen(Entity e) {
    return e.id >> GEN_SHIFT;
}
inline Entity make_entity(uint32_t index, uint32_t gen) {
    return Entity{(gen << GEN_SHIFT) | (index & INDEX_MASK)};
}
} // namespace ecs_detail

// ---------------------------------------------------------------------------
// EntityAllocator
// ---------------------------------------------------------------------------

/// @brief Manages entity slot allocation, deallocation, and generation tracking.
///
/// Slots are reused via a free list. Each reuse increments the generation
/// counter so stale handles from before the destroy are detectable via
/// `alive()`.
///
/// Used internally by `World`. Game code should call `World::create_entity()`
/// and `World::destroy()` rather than using this directly.
class EntityAllocator {
public:
    /// @brief Allocate a new entity slot (or reuse a freed one).
    Entity create() {
        if (!free_list_.empty()) {
            uint32_t idx = free_list_.back();
            free_list_.pop_back();
            return ecs_detail::make_entity(idx, generation_[idx]);
        }
        uint32_t idx = static_cast<uint32_t>(generation_.size());
        generation_.push_back(0);
        return ecs_detail::make_entity(idx, 0);
    }

    /// @brief Free an entity slot and increment its generation.
    ///
    /// After this call, any `Entity` handle with the old generation is stale
    /// and `alive()` will return false for it.
    void destroy(Entity e) {
        uint32_t idx = ecs_detail::entity_index(e);
        if (idx < generation_.size() && generation_[idx] == ecs_detail::entity_gen(e)) {
            generation_[idx]++;
            free_list_.push_back(idx);
        }
    }

    /// @brief Return true if @p e is a live entity (index and generation both match).
    bool alive(Entity e) const {
        uint32_t idx = ecs_detail::entity_index(e);
        return idx < generation_.size() && generation_[idx] == ecs_detail::entity_gen(e);
    }

    /// @brief Destroy all live entities and reset the allocator to empty state.
    ///
    /// After this call no entities are alive and `create()` will start fresh
    /// from slot 0 (generation 0).  Used by `World::restore()`.
    void reset() {
        generation_.clear();
        free_list_.clear();
    }

private:
    std::vector<uint32_t> generation_; // One entry per slot.
    std::vector<uint32_t> free_list_;  // Indices of destroyed slots.
};

// ---------------------------------------------------------------------------
// IComponentPool / ComponentPool<T>
// ---------------------------------------------------------------------------

/// @brief Type-erased interface for component pools (used by World internally).
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(Entity e) = 0;
    virtual bool has(Entity e) const = 0;
    /// @brief Remove all entries from this pool (used by World::restore()).
    virtual void clear_all() = 0;
    /// @brief Append all entities in this pool to @p out (used by World to rebuild bitmasks).
    virtual void enumerate_entities(std::vector<Entity>& out) const = 0;
};

/// @brief Sparse-set storage for components of type T.
///
/// - O(1) add / remove / lookup.
/// - Cache-friendly dense iteration via `World::each<T>()`.
/// - All components of the same type are stored contiguously in `dense_components_`.
///
/// Used internally by `World`. Do not instantiate directly.
///
/// Inherits `IComponentPool` **virtually** to allow `SerializableComponentPool<T>`
/// (in serial.hpp) to form a single-base diamond with `ISerializablePool`.
template<typename T>
class ComponentPool : public virtual IComponentPool {
public:
    /// @brief Add component @p value to entity @p e.
    /// Undefined behaviour if @p e already has this component.
    void add(Entity e, T value) {
        uint32_t idx = ecs_detail::entity_index(e);
        if (idx >= sparse_.size())
            sparse_.resize(idx + 1, EMPTY);
        sparse_[idx] = static_cast<uint32_t>(dense_entities_.size());
        dense_entities_.push_back(e);
        dense_components_.push_back(std::move(value));
    }

    /// @brief Remove the component from @p e. No-op if @p e has none.
    void remove(Entity e) override {
        uint32_t idx = ecs_detail::entity_index(e);
        if (idx >= sparse_.size() || sparse_[idx] == EMPTY)
            return;
        uint32_t dense_idx = sparse_[idx];
        uint32_t last = static_cast<uint32_t>(dense_entities_.size()) - 1;
        if (dense_idx != last) {
            dense_entities_[dense_idx] = dense_entities_[last];
            dense_components_[dense_idx] = std::move(dense_components_[last]);
            sparse_[ecs_detail::entity_index(dense_entities_[dense_idx])] = dense_idx;
        }
        dense_entities_.pop_back();
        dense_components_.pop_back();
        sparse_[idx] = EMPTY;
    }

    /// @brief Return true if entity @p e currently has this component.
    bool has(Entity e) const override {
        uint32_t idx = ecs_detail::entity_index(e);
        return idx < sparse_.size() && sparse_[idx] != EMPTY;
    }

    T& get(Entity e) { return dense_components_[sparse_[ecs_detail::entity_index(e)]]; }
    const T& get(Entity e) const { return dense_components_[sparse_[ecs_detail::entity_index(e)]]; }

    size_t size() const { return dense_entities_.size(); }
    Entity dense_entity(size_t i) const { return dense_entities_[i]; }
    T& dense_component(size_t i) { return dense_components_[i]; }
    const T& dense_component(size_t i) const { return dense_components_[i]; }

    /// @brief Remove all entries from this pool.
    void clear_all() override {
        sparse_.clear();
        dense_entities_.clear();
        dense_components_.clear();
    }

    /// @brief Append all entities in this pool to @p out.
    void enumerate_entities(std::vector<Entity>& out) const override {
        out.insert(out.end(), dense_entities_.begin(), dense_entities_.end());
    }

private:
    static constexpr uint32_t EMPTY = UINT32_MAX;
    std::vector<uint32_t> sparse_;
    std::vector<Entity> dense_entities_;
    std::vector<T> dense_components_;
};

} // namespace xebble
