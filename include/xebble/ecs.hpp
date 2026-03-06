/// @file ecs.hpp
/// @brief Entity Component System core types: Entity handle, component storage.
#pragma once

#include <climits>
#include <cstdint>
#include <span>
#include <vector>

namespace xebble {

/// @brief Opaque entity handle with generational index.
struct Entity {
    uint32_t id = 0;
    bool operator==(Entity other) const { return id == other.id; }
    bool operator!=(Entity other) const { return id != other.id; }
};

class Event;

/// @brief Resource providing input events for the current frame.
struct EventQueue {
    std::span<const Event> events;
};

namespace ecs_detail {
    constexpr uint32_t INDEX_BITS = 20;
    constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1;
    constexpr uint32_t GEN_SHIFT = INDEX_BITS;

    inline uint32_t entity_index(Entity e) { return e.id & INDEX_MASK; }
    inline uint32_t entity_gen(Entity e) { return e.id >> GEN_SHIFT; }
    inline Entity make_entity(uint32_t index, uint32_t gen) {
        return Entity{(gen << GEN_SHIFT) | (index & INDEX_MASK)};
    }
} // namespace ecs_detail

/// @brief Manages entity creation, destruction, and slot reuse.
class EntityAllocator {
public:
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

    void destroy(Entity e) {
        uint32_t idx = ecs_detail::entity_index(e);
        if (idx < generation_.size() && generation_[idx] == ecs_detail::entity_gen(e)) {
            generation_[idx]++;
            free_list_.push_back(idx);
        }
    }

    bool alive(Entity e) const {
        uint32_t idx = ecs_detail::entity_index(e);
        return idx < generation_.size() && generation_[idx] == ecs_detail::entity_gen(e);
    }

private:
    std::vector<uint32_t> generation_;
    std::vector<uint32_t> free_list_;
};

/// @brief Type-erased base for component pools.
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(Entity e) = 0;
    virtual bool has(Entity e) const = 0;
};

/// @brief Sparse set storage for components of type T.
template<typename T>
class ComponentPool : public IComponentPool {
public:
    void add(Entity e, T value) {
        uint32_t idx = ecs_detail::entity_index(e);
        if (idx >= sparse_.size()) {
            sparse_.resize(idx + 1, EMPTY);
        }
        sparse_[idx] = static_cast<uint32_t>(dense_entities_.size());
        dense_entities_.push_back(e);
        dense_components_.push_back(std::move(value));
    }

    void remove(Entity e) override {
        uint32_t idx = ecs_detail::entity_index(e);
        if (idx >= sparse_.size() || sparse_[idx] == EMPTY) return;

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

    bool has(Entity e) const override {
        uint32_t idx = ecs_detail::entity_index(e);
        return idx < sparse_.size() && sparse_[idx] != EMPTY;
    }

    T& get(Entity e) {
        return dense_components_[sparse_[ecs_detail::entity_index(e)]];
    }

    const T& get(Entity e) const {
        return dense_components_[sparse_[ecs_detail::entity_index(e)]];
    }

    size_t size() const { return dense_entities_.size(); }

    Entity dense_entity(size_t i) const { return dense_entities_[i]; }
    T& dense_component(size_t i) { return dense_components_[i]; }
    const T& dense_component(size_t i) const { return dense_components_[i]; }

private:
    static constexpr uint32_t EMPTY = UINT32_MAX;
    std::vector<uint32_t> sparse_;
    std::vector<Entity> dense_entities_;
    std::vector<T> dense_components_;
};

} // namespace xebble
