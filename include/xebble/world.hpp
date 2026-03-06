/// @file world.hpp
/// @brief ECS World — central coordinator for entities, components, systems, and resources.
#pragma once

#include <xebble/ecs.hpp>
#include <xebble/system.hpp>

#include <any>
#include <cassert>
#include <memory>
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

/// @brief Fluent builder for creating entities with components.
class EntityBuilder {
public:
    explicit EntityBuilder(World& world, Entity entity)
        : world_(world), entity_(entity) {}

    template<typename T>
    EntityBuilder& with(T value);

    Entity build() { return entity_; }

private:
    World& world_;
    Entity entity_;
};

/// @brief Central ECS coordinator.
class World {
public:
    // --- Entities ---
    Entity create_entity() { return allocator_.create(); }
    EntityBuilder build_entity() { return EntityBuilder(*this, create_entity()); }
    void destroy(Entity e) { pending_destroy_.push_back(e); }
    bool alive(Entity e) const { return allocator_.alive(e); }

    // --- Components ---
    template<typename T>
    void register_component() {
        auto id = ecs_detail::component_id<T>();
        if (id >= pools_.size()) pools_.resize(id + 1);
        pools_[id] = std::make_unique<ComponentPool<T>>();
    }

    template<typename T>
    T& add(Entity e, T value) {
        auto& pool = get_pool<T>();
        pool.add(e, std::move(value));
        return pool.get(e);
    }

    template<typename T>
    void remove(Entity e) {
        get_pool<T>().remove(e);
    }

    template<typename T>
    T& get(Entity e) {
        return get_pool<T>().get(e);
    }

    template<typename T>
    const T& get(Entity e) const {
        return get_pool<T>().get(e);
    }

    template<typename T>
    bool has(Entity e) const {
        auto id = ecs_detail::component_id<T>();
        if (id >= pools_.size() || !pools_[id]) return false;
        return static_cast<const ComponentPool<T>*>(pools_[id].get())->has(e);
    }

    // --- Iteration ---
    template<typename T, typename Fn>
    void each(Fn&& fn) {
        auto& pool = get_pool<T>();
        for (size_t i = 0; i < pool.size(); i++) {
            fn(pool.dense_entity(i), pool.dense_component(i));
        }
    }

    template<typename T1, typename T2, typename... Rest, typename Fn>
    void each(Fn&& fn) {
        auto& pool = get_pool<T1>();
        for (size_t i = 0; i < pool.size(); i++) {
            Entity e = pool.dense_entity(i);
            if (has<T2>(e) && (has<Rest>(e) && ...)) {
                fn(e, pool.dense_component(i), get<T2>(e), get<Rest>(e)...);
            }
        }
    }

    // --- Resources ---
    template<typename T>
    void add_resource(T value) {
        auto id = ecs_detail::component_id<T>();
        resources_[id] = std::move(value);
    }

    template<typename T>
    void remove_resource() {
        resources_.erase(ecs_detail::component_id<T>());
    }

    template<typename T>
    T& resource() {
        return std::any_cast<T&>(resources_.at(ecs_detail::component_id<T>()));
    }

    template<typename T>
    const T& resource() const {
        return std::any_cast<const T&>(resources_.at(ecs_detail::component_id<T>()));
    }

    template<typename T>
    bool has_resource() const {
        return resources_.contains(ecs_detail::component_id<T>());
    }

    // --- Systems ---
    template<typename T, typename... Args>
    void add_system(Args&&... args) {
        systems_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
    }

    template<typename T, typename... Args>
    void prepend_system(Args&&... args) {
        systems_.insert(systems_.begin(), std::make_unique<T>(std::forward<Args>(args)...));
    }

    void init_systems();
    void tick_update(float dt);
    void tick_draw(Renderer& renderer);
    void flush_destroyed();

private:
    template<typename T>
    ComponentPool<T>& get_pool() {
        auto id = ecs_detail::component_id<T>();
        assert(id < pools_.size() && pools_[id] && "Component type not registered");
        return static_cast<ComponentPool<T>&>(*pools_[id]);
    }

    template<typename T>
    const ComponentPool<T>& get_pool() const {
        auto id = ecs_detail::component_id<T>();
        assert(id < pools_.size() && pools_[id] && "Component type not registered");
        return static_cast<const ComponentPool<T>&>(*pools_[id]);
    }

    EntityAllocator allocator_;
    std::vector<std::unique_ptr<IComponentPool>> pools_;
    std::unordered_map<ecs_detail::ComponentId, std::any> resources_;
    std::vector<std::unique_ptr<System>> systems_;
    std::vector<Entity> pending_destroy_;
};

// EntityBuilder::with implementation (needs World to be complete)
template<typename T>
EntityBuilder& EntityBuilder::with(T value) {
    world_.add<T>(entity_, std::move(value));
    return *this;
}

} // namespace xebble
