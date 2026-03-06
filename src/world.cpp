/// @file world.cpp
/// @brief World non-template method implementations.
#include <xebble/world.hpp>
#include <xebble/renderer.hpp>

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
    for (Entity e : pending_destroy_) {
        if (!allocator_.alive(e)) continue;
        for (auto& pool : pools_) {
            if (pool && pool->has(e)) {
                pool->remove(e);
            }
        }
        allocator_.destroy(e);
    }
    pending_destroy_.clear();
}

} // namespace xebble
