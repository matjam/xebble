/// @file world.cpp
/// @brief World non-template method implementations.
#include <xebble/renderer.hpp>
#include <xebble/world.hpp>

#include <algorithm>
#include <cstring>

namespace xebble {

void World::init_systems() {
    for (auto& sys : systems_) {
        sys->init(*this);
    }
}

void World::tick_update(float dt) {
    for (auto& sys : systems_) {
        sys->update(*this, dt);
    }
    flush_destroyed();
}

void World::tick_draw(Renderer& renderer) {
    for (auto& sys : systems_) {
        sys->draw(*this, renderer);
    }
}

void World::flush_destroyed() {
    bool any_destroyed = false;
    for (Entity e : pending_destroy_) {
        if (!allocator_.alive(e))
            continue;
        for (auto& pool : pools_) {
            if (pool && pool->has(e)) {
                pool->remove(e);
            }
        }
        clear_mask(ecs_detail::entity_index(e));
        allocator_.destroy(e);
        any_destroyed = true;
    }
    pending_destroy_.clear();
    if (any_destroyed)
        ++generation_;
}

// ---------------------------------------------------------------------------
// snapshot()
// ---------------------------------------------------------------------------

std::vector<uint8_t> World::snapshot() const {
    using namespace serial_detail;

    std::vector<uint8_t> out;
    out.reserve(4096);

    // ---- Header (placeholder — patched below) ----
    const size_t header_offset = 0;
    uint32_t magic = MAGIC;
    uint32_t version = VERSION;
    uint32_t entity_count = 0;
    uint32_t pool_count = 0;
    uint32_t resource_count = 0;
    append(out, magic);
    append(out, version);
    append(out, entity_count);   // patched
    append(out, pool_count);     // patched
    append(out, resource_count); // patched

    // ---- Collect live entities from serializable pools ----
    // We build a sorted-unique list of (slot) across all serializable pools.
    std::vector<uint32_t> slots;
    for (const auto& pool_ptr : pools_) {
        if (!pool_ptr)
            continue;
        auto* sp = dynamic_cast<const ISerializablePool*>(pool_ptr.get());
        if (!sp)
            continue;
        std::vector<Entity> ents;
        sp->enumerate_entities(ents);
        for (Entity e : ents)
            slots.push_back(ecs_detail::entity_index(e));
    }
    std::sort(slots.begin(), slots.end());
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());

    // ---- Entity table ----
    entity_count = static_cast<uint32_t>(slots.size());
    for (uint32_t s : slots) {
        append(out, s);
        append(out, uint32_t{0}); // generation placeholder (fresh on restore)
    }

    // ---- Component sections ----
    // Format per section:
    //   name_len    : uint16_t
    //   name        : char[name_len]
    //   record_count: uint32_t
    //   data_len    : uint32_t   ← allows unknown pools to be skipped
    //   data        : uint8_t[data_len]

    for (const auto& pool_ptr : pools_) {
        if (!pool_ptr)
            continue;
        auto* sp = dynamic_cast<const ISerializablePool*>(pool_ptr.get());
        if (!sp)
            continue;

        std::vector<uint8_t> section_data;
        uint32_t rc = 0;
        sp->serialize_all(section_data, rc);
        if (rc == 0)
            continue; // skip empty pools

        append_string(out, sp->component_name());
        append(out, rc);
        auto data_len = static_cast<uint32_t>(section_data.size());
        append(out, data_len);
        out.insert(out.end(), section_data.begin(), section_data.end());
        ++pool_count;
    }

    // ---- Resource sections ----
    // Format per section:
    //   name_len    : uint16_t
    //   name        : char[name_len]
    //   data_len    : uint32_t
    //   data        : uint8_t[data_len]

    for (const auto& [id, ser] : resource_serializers_) {
        auto it = resources_.find(id);
        if (it == resources_.end())
            continue;

        std::vector<uint8_t> res_bytes;
        ser.write(it->second, res_bytes);

        append_string(out, ser.name);
        auto data_len = static_cast<uint32_t>(res_bytes.size());
        append(out, data_len);
        out.insert(out.end(), res_bytes.begin(), res_bytes.end());
        ++resource_count;
    }

    // ---- Patch header ----
    std::memcpy(out.data() + header_offset + sizeof(uint32_t) * 2, &entity_count, sizeof(uint32_t));
    std::memcpy(out.data() + header_offset + sizeof(uint32_t) * 3, &pool_count, sizeof(uint32_t));
    std::memcpy(out.data() + header_offset + sizeof(uint32_t) * 4, &resource_count,
                sizeof(uint32_t));

    return out;
}

// ---------------------------------------------------------------------------
// restore()
// ---------------------------------------------------------------------------

std::expected<void, Error> World::restore(std::span<const uint8_t> blob) {
    using namespace serial_detail;

    const uint8_t* data = blob.data();
    const size_t size = blob.size();
    size_t offset = 0;

    // ---- Validate header ----
    uint32_t magic = 0, version = 0, entity_count = 0, pool_count = 0, resource_count = 0;
    if (!read(data, size, offset, magic) || magic != MAGIC || !read(data, size, offset, version) ||
        version != VERSION || !read(data, size, offset, entity_count) ||
        !read(data, size, offset, pool_count) || !read(data, size, offset, resource_count)) {
        return std::unexpected(Error{"restore: corrupt or incompatible header"});
    }

    // ---- Clear all existing entity/component state ----
    // Discard any pending deferred destroys, then clear every pool
    // (serializable and non-serializable alike) and reset the allocator.
    // This gives us a blank slate so that recreated entity slot indices
    // map exactly 1-to-1 with the saved slots.
    pending_destroy_.clear();
    for (const auto& pool_ptr : pools_) {
        if (pool_ptr)
            pool_ptr->clear_all();
    }
    allocator_.reset();
    component_masks_.clear();

    // ---- Read entity table ----
    // The saved slots are the canonical indices we want restored entities to
    // occupy.  After allocator_.reset(), allocator_.create() issues slots
    // 0, 1, 2, … in order — so we sort the saved slots and create entities
    // in ascending order to get back a matching slot→entity mapping.
    uint32_t max_slot = 0;
    std::vector<uint32_t> saved_slots(entity_count);
    for (uint32_t i = 0; i < entity_count; ++i) {
        uint32_t slot = 0, gen = 0;
        if (!read(data, size, offset, slot) || !read(data, size, offset, gen)) {
            return std::unexpected(Error{"restore: truncated entity table"});
        }
        saved_slots[i] = slot;
        max_slot = std::max(max_slot, slot);
    }

    // Build a set of which slots need to be alive after restore.
    std::vector<bool> slot_needed(max_slot + 1, false);
    for (uint32_t s : saved_slots)
        slot_needed[s] = true;

    // After reset(), allocator_.create() issues slot 0, 1, 2, … in order.
    // Allocate every slot from 0 to max_slot; keep the ones that were saved,
    // immediately destroy the gap slots so they return to the free list.
    std::vector<Entity> entity_map(max_slot + 1, Entity{UINT32_MAX});
    for (uint32_t s = 0; s <= max_slot; ++s) {
        Entity e = allocator_.create(); // slot index == s (gen 0)
        if (slot_needed[s]) {
            entity_map[s] = e; // keep: restored entity
        } else {
            allocator_.destroy(e); // gap: free immediately
        }
    }

    // ---- Read component sections ----
    for (uint32_t p = 0; p < pool_count; ++p) {
        std::string name;
        if (!read_string(data, size, offset, name))
            return std::unexpected(Error{"restore: truncated pool name"});

        uint32_t record_count = 0, data_len = 0;
        if (!read(data, size, offset, record_count) || !read(data, size, offset, data_len)) {
            return std::unexpected(Error{"restore: truncated pool header"});
        }
        if (offset + data_len > size)
            return std::unexpected(Error{"restore: truncated pool data for '" + name + "'"});

        // Find matching pool.
        ISerializablePool* target_pool = nullptr;
        for (const auto& pool_ptr : pools_) {
            if (!pool_ptr)
                continue;
            auto* sp = dynamic_cast<ISerializablePool*>(pool_ptr.get());
            if (sp && sp->component_name() == name) {
                target_pool = sp;
                break;
            }
        }

        if (target_pool) {
            // Sanity check: data_len must equal record_count * bytes_per_record.
            size_t expected_bytes =
                static_cast<size_t>(record_count) * target_pool->bytes_per_record();
            if (data_len != expected_bytes) {
                return std::unexpected(Error{"restore: pool '" + name + "' size mismatch"});
            }
            target_pool->deserialize_all(data + offset, data_len, entity_map);
        }
        // else: unknown pool type — skip (data_len lets us advance safely).

        offset += data_len;
    }

    // ---- Read resource sections ----
    for (uint32_t r = 0; r < resource_count; ++r) {
        std::string name;
        if (!read_string(data, size, offset, name))
            return std::unexpected(Error{"restore: truncated resource name"});

        uint32_t data_len = 0;
        if (!read(data, size, offset, data_len))
            return std::unexpected(Error{"restore: truncated resource data length"});
        if (offset + data_len > size)
            return std::unexpected(Error{"restore: truncated resource data for '" + name + "'"});

        // Find matching serializer.
        for (auto& [id, ser] : resource_serializers_) {
            if (ser.name == name) {
                auto it = resources_.find(id);
                if (it != resources_.end())
                    ser.read(it->second, data + offset, data_len);
                break;
            }
        }
        // Unknown resource — skip (data_len lets us advance safely).
        offset += data_len;
    }

    // ---- Rebuild component bitmasks ----
    // Pools were populated by deserialize_all() which calls pool->add()
    // directly — those calls bypass World::add<T>() and therefore don't
    // set bitmask bits.  Rebuild the full mask from every pool.
    {
        std::vector<Entity> ents;
        for (uint32_t pool_id = 0; pool_id < pools_.size(); ++pool_id) {
            if (!pools_[pool_id])
                continue;
            ents.clear();
            pools_[pool_id]->enumerate_entities(ents);
            for (Entity e : ents) {
                uint32_t slot = ecs_detail::entity_index(e);
                ensure_mask_slot(slot);
                set_mask_bit(slot, pool_id);
            }
        }
    }

    ++generation_;
    return {};
}

} // namespace xebble
