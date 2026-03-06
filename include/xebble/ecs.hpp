/// @file ecs.hpp
/// @brief Entity Component System core types: Entity handle, component storage.
#pragma once

#include <cstdint>
#include <vector>

namespace xebble {

/// @brief Opaque entity handle with generational index.
struct Entity {
    uint32_t id = 0;
    bool operator==(Entity other) const { return id == other.id; }
    bool operator!=(Entity other) const { return id != other.id; }
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

} // namespace xebble
