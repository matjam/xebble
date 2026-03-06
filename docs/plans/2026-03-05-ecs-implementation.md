# ECS Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a first-class Entity Component System to xebble, replacing the Game base class with a World that coordinates entities, components, systems, and resources.

**Architecture:** Sparse-set component storage with generational entity IDs. World is the central coordinator; systems declare init/update/draw; resources provide singleton access to engine services. `xebble::run()` accepts a World instead of a Game.

**Tech Stack:** C++23, GoogleTest, CMake

---

### Task 1: Entity and Generational ID

**Files:**
- Create: `include/xebble/ecs.hpp`
- Create: `tests/test_ecs.cpp`
- Modify: `tests/CMakeLists.txt`

This task implements the Entity handle type with generational indices and the EntityAllocator that manages slot reuse.

**Step 1: Write the failing tests**

Create `tests/test_ecs.cpp`:

```cpp
#include <gtest/gtest.h>
#include <xebble/ecs.hpp>

using namespace xebble;

TEST(Entity, DefaultIsNull) {
    Entity e{};
    EXPECT_EQ(e.id, 0u);
}

TEST(Entity, Equality) {
    Entity a{1};
    Entity b{1};
    Entity c{2};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(EntityAllocator, CreateSequentialEntities) {
    EntityAllocator alloc;
    auto e0 = alloc.create();
    auto e1 = alloc.create();
    // Different entities
    EXPECT_NE(e0, e1);
    // Both alive
    EXPECT_TRUE(alloc.alive(e0));
    EXPECT_TRUE(alloc.alive(e1));
}

TEST(EntityAllocator, DestroyAndRecycleSlot) {
    EntityAllocator alloc;
    auto e0 = alloc.create();
    alloc.destroy(e0);
    EXPECT_FALSE(alloc.alive(e0));

    // Creating a new entity may reuse the slot, but generation differs
    auto e1 = alloc.create();
    EXPECT_TRUE(alloc.alive(e1));
    EXPECT_NE(e0, e1);  // different generation
}

TEST(EntityAllocator, StaleHandleNotAlive) {
    EntityAllocator alloc;
    auto e0 = alloc.create();
    alloc.destroy(e0);
    auto e1 = alloc.create();  // reuses slot
    // Old handle should not be alive even though slot is reused
    EXPECT_FALSE(alloc.alive(e0));
    EXPECT_TRUE(alloc.alive(e1));
}
```

**Step 2: Add test file to CMake**

Modify `tests/CMakeLists.txt` — add `test_ecs.cpp` to the source list:

```cmake
add_executable(xebble_tests
    test_types.cpp
    test_event.cpp
    test_spritesheet.cpp
    test_tilemap.cpp
    test_font.cpp
    test_asset_manager.cpp
    test_ecs.cpp
)
```

**Step 3: Run tests to verify they fail**

Run: `cmake --build build/debug --target xebble_tests 2>&1`
Expected: Compilation error — `xebble/ecs.hpp` not found.

**Step 4: Write minimal implementation**

Create `include/xebble/ecs.hpp`:

```cpp
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
    // Entity ID layout: lower 20 bits = index, upper 12 bits = generation
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
```

**Step 5: Run tests to verify they pass**

Run: `cmake --build build/debug --target xebble_tests && ctest --test-dir build/debug -R Entity`
Expected: All Entity and EntityAllocator tests PASS.

**Step 6: Commit**

```bash
git add include/xebble/ecs.hpp tests/test_ecs.cpp tests/CMakeLists.txt
git commit -m "feat(ecs): add Entity handle and EntityAllocator with generational IDs"
```

---

### Task 2: Sparse Set ComponentPool

**Files:**
- Modify: `include/xebble/ecs.hpp`
- Modify: `tests/test_ecs.cpp`

This task implements the sparse set data structure (`ComponentPool<T>`) and the type-erased `IComponentPool` base.

**Step 1: Write the failing tests**

Append to `tests/test_ecs.cpp`:

```cpp
TEST(ComponentPool, AddAndGet) {
    struct Pos { int x, y; };
    ComponentPool<Pos> pool;
    Entity e{1};
    pool.add(e, Pos{10, 20});
    EXPECT_TRUE(pool.has(e));
    EXPECT_EQ(pool.get(e).x, 10);
    EXPECT_EQ(pool.get(e).y, 20);
}

TEST(ComponentPool, Remove) {
    struct Pos { int x, y; };
    ComponentPool<Pos> pool;
    Entity e{1};
    pool.add(e, Pos{10, 20});
    pool.remove(e);
    EXPECT_FALSE(pool.has(e));
}

TEST(ComponentPool, IterateDenseArray) {
    struct Pos { int x, y; };
    ComponentPool<Pos> pool;
    Entity e0{0}, e1{1}, e2{2};
    pool.add(e0, Pos{1, 1});
    pool.add(e1, Pos{2, 2});
    pool.add(e2, Pos{3, 3});

    int sum = 0;
    for (size_t i = 0; i < pool.size(); i++) {
        sum += pool.dense_component(i).x;
    }
    EXPECT_EQ(sum, 6);
}

TEST(ComponentPool, RemoveSwapsLast) {
    struct Val { int v; };
    ComponentPool<Val> pool;
    Entity e0{0}, e1{1}, e2{2};
    pool.add(e0, Val{10});
    pool.add(e1, Val{20});
    pool.add(e2, Val{30});
    pool.remove(e0);
    // After removal, dense array should be packed (size 2)
    EXPECT_EQ(pool.size(), 2u);
    // Both remaining entities should still be accessible
    EXPECT_TRUE(pool.has(e1));
    EXPECT_TRUE(pool.has(e2));
    EXPECT_EQ(pool.get(e1).v, 20);
    EXPECT_EQ(pool.get(e2).v, 30);
}

TEST(ComponentPool, ManyEntities) {
    struct Id { uint32_t v; };
    ComponentPool<Id> pool;
    for (uint32_t i = 0; i < 1000; i++) {
        pool.add(Entity{i}, Id{i * 2});
    }
    EXPECT_EQ(pool.size(), 1000u);
    for (uint32_t i = 0; i < 1000; i++) {
        EXPECT_EQ(pool.get(Entity{i}).v, i * 2);
    }
}
```

**Step 2: Run tests to verify they fail**

Run: `cmake --build build/debug --target xebble_tests 2>&1`
Expected: Compilation error — `ComponentPool` not defined.

**Step 3: Write minimal implementation**

Add to `include/xebble/ecs.hpp`, after EntityAllocator:

```cpp
/// @brief Type-erased base for component pools.
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(Entity e) = 0;
    virtual bool has(Entity e) const = 0;
};

/// @brief Sparse set storage for components of type T.
///
/// Provides O(1) add/remove/has/get and cache-friendly iteration via a packed
/// dense array. Uses swap-and-pop for stable O(1) removal.
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
            // Swap with last element
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
    std::vector<uint32_t> sparse_;        // entity index -> dense index
    std::vector<Entity> dense_entities_;   // packed entity handles
    std::vector<T> dense_components_;      // packed component data
};
```

**Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug --target xebble_tests && ctest --test-dir build/debug -R ComponentPool`
Expected: All ComponentPool tests PASS.

**Step 5: Commit**

```bash
git add include/xebble/ecs.hpp tests/test_ecs.cpp
git commit -m "feat(ecs): add sparse set ComponentPool with type-erased base"
```

---

### Task 3: System Base Class

**Files:**
- Create: `include/xebble/system.hpp`

This task creates the System base class with `init()`, `update()`, and `draw()` virtual methods.

**Step 1: Create the header**

Create `include/xebble/system.hpp`:

```cpp
/// @file system.hpp
/// @brief System base class for ECS game logic and rendering.
#pragma once

namespace xebble {

class World;
class Renderer;

/// @brief Base class for ECS systems.
///
/// Override one or more methods. Systems run in registration order.
/// init() is called once after engine resources are available.
/// update() is called at fixed timestep. draw() is called each frame.
class System {
public:
    virtual ~System() = default;

    /// @brief Called once after all systems are added and engine resources are available.
    virtual void init(World& world) {}

    /// @brief Called at fixed timestep for game logic.
    virtual void update(World& world, float dt) {}

    /// @brief Called each frame for rendering.
    virtual void draw(World& world, Renderer& renderer) {}
};

} // namespace xebble
```

This is a trivial header with no tests needed (no logic, just a virtual interface). But we verify it compiles.

**Step 2: Verify it compiles**

Run: `cmake --build build/debug --target xebble_tests 2>&1`
Expected: Compiles cleanly (header not yet included by anything, but no errors).

**Step 3: Commit**

```bash
git add include/xebble/system.hpp
git commit -m "feat(ecs): add System base class with init/update/draw"
```

---

### Task 4: World — Core API

**Files:**
- Create: `include/xebble/world.hpp`
- Create: `src/world.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/test_ecs.cpp`

This is the biggest task. The World ties together entities, components, resources, and systems.

**Step 1: Write the failing tests**

Append to `tests/test_ecs.cpp` (add `#include <xebble/world.hpp>` at top):

```cpp
#include <xebble/world.hpp>
#include <xebble/system.hpp>

// --- Test components ---
namespace {
    struct Position { int x, y; };
    struct Velocity { int dx, dy; };
    struct Health { int hp; };
}

// --- World entity tests ---
TEST(World, CreateAndDestroyEntity) {
    World world;
    world.register_component<Position>();

    auto e = world.create_entity();
    EXPECT_TRUE(world.alive(e));
    world.add<Position>(e, {10, 20});
    EXPECT_TRUE(world.has<Position>(e));
    EXPECT_EQ(world.get<Position>(e).x, 10);

    world.destroy(e);
    world.flush_destroyed();  // normally called by tick_update
    EXPECT_FALSE(world.alive(e));
}

TEST(World, EntityBuilder) {
    World world;
    world.register_component<Position>();
    world.register_component<Velocity>();

    auto e = world.build_entity()
        .with<Position>({5, 10})
        .with<Velocity>({1, -1})
        .build();

    EXPECT_TRUE(world.alive(e));
    EXPECT_EQ(world.get<Position>(e).x, 5);
    EXPECT_EQ(world.get<Velocity>(e).dx, 1);
}

// --- World each() tests ---
TEST(World, EachSingleComponent) {
    World world;
    world.register_component<Position>();

    world.add<Position>(world.create_entity(), {1, 0});
    world.add<Position>(world.create_entity(), {2, 0});
    world.add<Position>(world.create_entity(), {3, 0});

    int sum = 0;
    world.each<Position>([&](Entity, Position& p) { sum += p.x; });
    EXPECT_EQ(sum, 6);
}

TEST(World, EachMultipleComponents) {
    World world;
    world.register_component<Position>();
    world.register_component<Velocity>();

    auto e0 = world.create_entity();
    world.add<Position>(e0, {1, 0});
    world.add<Velocity>(e0, {10, 0});

    auto e1 = world.create_entity();
    world.add<Position>(e1, {2, 0});
    // e1 has no Velocity

    int count = 0;
    world.each<Position, Velocity>([&](Entity, Position& p, Velocity& v) {
        count++;
        EXPECT_EQ(p.x, 1);
        EXPECT_EQ(v.dx, 10);
    });
    EXPECT_EQ(count, 1);
}

// --- Resource tests ---
TEST(World, Resources) {
    struct GameState { int score; };

    World world;
    world.add_resource<GameState>(GameState{42});
    EXPECT_TRUE(world.has_resource<GameState>());
    EXPECT_EQ(world.resource<GameState>().score, 42);

    world.resource<GameState>().score = 100;
    EXPECT_EQ(world.resource<GameState>().score, 100);

    world.remove_resource<GameState>();
    EXPECT_FALSE(world.has_resource<GameState>());
}

// --- System tests ---
TEST(World, SystemInitAndUpdate) {
    struct Counter { int value = 0; };

    struct CountSystem : System {
        void init(World& w) override {
            w.resource<Counter>().value = 10;
        }
        void update(World& w, float) override {
            w.resource<Counter>().value++;
        }
    };

    World world;
    world.add_resource<Counter>({});
    world.add_system<CountSystem>();
    world.init_systems();
    EXPECT_EQ(world.resource<Counter>().value, 10);

    world.tick_update(1.0f / 60.0f);
    EXPECT_EQ(world.resource<Counter>().value, 11);
}

TEST(World, DeferredDestruction) {
    World world;
    world.register_component<Position>();

    auto e = world.create_entity();
    world.add<Position>(e, {1, 2});
    world.destroy(e);

    // Still alive until flush
    EXPECT_TRUE(world.alive(e));
    EXPECT_TRUE(world.has<Position>(e));

    world.flush_destroyed();
    EXPECT_FALSE(world.alive(e));
}
```

**Step 2: Run tests to verify they fail**

Run: `cmake --build build/debug --target xebble_tests 2>&1`
Expected: Compilation error — `xebble/world.hpp` not found.

**Step 3: Write the World header**

Create `include/xebble/world.hpp`:

```cpp
/// @file world.hpp
/// @brief ECS World — central coordinator for entities, components, systems, and resources.
#pragma once

#include <xebble/ecs.hpp>
#include <xebble/system.hpp>

#include <any>
#include <cassert>
#include <functional>
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
    template<typename T>
    void each(std::function<void(Entity, T&)> fn) {
        auto& pool = get_pool<T>();
        for (size_t i = 0; i < pool.size(); i++) {
            fn(pool.dense_entity(i), pool.dense_component(i));
        }
    }

    template<typename T1, typename T2, typename... Rest>
    void each(std::function<void(Entity, T1&, T2&, Rest&...)> fn) {
        // Iterate over the first component type's pool, filter by the rest
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
```

**Step 4: Write the World source file**

Create `src/world.cpp`:

```cpp
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
```

**Step 5: Add world.cpp to CMake**

Modify `src/CMakeLists.txt` — add `world.cpp` to the source list:

```cmake
add_library(xebble STATIC
    log.cpp
    window.cpp
    vulkan/vma_impl.cpp
    vulkan/context.cpp
    vulkan/swapchain.cpp
    vulkan/buffer.cpp
    vulkan/descriptor.cpp
    vulkan/command.cpp
    vulkan/pipeline.cpp
    stb_impl.cpp
    texture.cpp
    renderer.cpp
    spritesheet.cpp
    sprite.cpp
    tilemap.cpp
    font.cpp
    asset_manager.cpp
    game.cpp
    world.cpp
)
```

**Step 6: Run tests to verify they pass**

Run: `cmake --build build/debug --target xebble_tests && ctest --test-dir build/debug -R World`
Expected: All World tests PASS.

**Step 7: Commit**

```bash
git add include/xebble/world.hpp src/world.cpp src/CMakeLists.txt tests/test_ecs.cpp
git commit -m "feat(ecs): add World with entities, components, resources, systems, and entity builder"
```

---

### Task 5: Update Umbrella Header and run() Function

**Files:**
- Modify: `include/xebble/xebble.hpp`
- Modify: `include/xebble/game.hpp`
- Modify: `src/game.cpp`

This task adds the new ECS headers to the umbrella and adds a second `run()` overload that accepts a World. The Game-based `run()` is kept for now (removed in Task 7 when the example is rewritten).

**Step 1: Update the umbrella header**

Modify `include/xebble/xebble.hpp` — add after the `game.hpp` include:

```cpp
#include <xebble/ecs.hpp>
#include <xebble/system.hpp>
#include <xebble/world.hpp>
```

**Step 2: Add the World-based run() declaration**

Modify `include/xebble/game.hpp` — add after the existing `run()` declaration (line 58), before the closing namespace brace:

```cpp
/// @brief Create systems and run the game loop with an ECS World.
/// @param world The ECS world with registered components and systems.
/// @param config Full game configuration.
/// @return 0 on success, non-zero on failure.
int run(World world, const GameConfig& config);
```

Also add `#include <xebble/world.hpp>` to the includes at the top.

**Step 3: Implement the World-based run()**

Append to `src/game.cpp`, inside the `xebble` namespace, after the existing `run()`:

```cpp
/// @brief Event queue resource provided to systems each frame.
struct EventQueue {
    std::span<const Event> events;
};

int run(World world, const GameConfig& config) {
    auto window = Window::create(config.window);
    if (!window) {
        log(LogLevel::Error, "Failed to create window: " + window.error().message);
        return 1;
    }

    auto renderer = Renderer::create(*window, config.renderer);
    if (!renderer) {
        log(LogLevel::Error, "Failed to create renderer: " + renderer.error().message);
        return 1;
    }

    auto assets = AssetManager::create(renderer->context(), config.assets);
    if (!assets) {
        log(LogLevel::Error, "Failed to create asset manager: " + assets.error().message);
        return 1;
    }

    // Add engine resources
    world.add_resource<Renderer*>(&*renderer);
    world.add_resource<AssetManager*>(&*assets);
    world.add_resource<EventQueue>(EventQueue{});

    world.init_systems();

    float accumulator = 0.0f;

    while (!window->should_close()) {
        window->poll_events();
        world.resource<EventQueue>().events = window->events();

        accumulator += renderer->delta_time();
        while (accumulator >= config.fixed_timestep) {
            world.tick_update(config.fixed_timestep);
            accumulator -= config.fixed_timestep;
        }

        if (renderer->begin_frame()) {
            world.tick_draw(*renderer);
            renderer->end_frame();
        }
    }

    return 0;
}
```

Note: Renderer and AssetManager are stored as pointer resources (not moved in) because they're stack-owned by `run()`. Systems access them via `world.resource<Renderer*>()`. This avoids lifetime issues.

**Step 4: Add required includes to game.cpp**

Add to the top of `src/game.cpp`:

```cpp
#include <xebble/world.hpp>
#include <span>
```

**Step 5: Verify everything compiles**

Run: `cmake --build build/debug && ctest --test-dir build/debug`
Expected: Full build succeeds, all existing tests pass.

**Step 6: Commit**

```bash
git add include/xebble/xebble.hpp include/xebble/game.hpp src/game.cpp
git commit -m "feat(ecs): add World-based run() overload and update umbrella header"
```

---

### Task 6: Move EventQueue to Public Header

**Files:**
- Modify: `include/xebble/world.hpp` (or `include/xebble/ecs.hpp`)
- Modify: `src/game.cpp`

The `EventQueue` struct should be in a public header so user systems can reference it.

**Step 1: Add EventQueue to ecs.hpp**

Add to `include/xebble/ecs.hpp`, after the Entity struct:

```cpp
#include <span>

// Forward declare Event
class Event;

/// @brief Resource providing input events for the current frame.
struct EventQueue {
    std::span<const Event> events;
};
```

**Step 2: Remove EventQueue definition from game.cpp**

Remove the `struct EventQueue` definition from `src/game.cpp` (it's now in the header).

**Step 3: Verify build**

Run: `cmake --build build/debug && ctest --test-dir build/debug`
Expected: Compiles and all tests pass.

**Step 4: Commit**

```bash
git add include/xebble/ecs.hpp src/game.cpp
git commit -m "refactor(ecs): move EventQueue to public header"
```

---

### Task 7: Rewrite Example to Use ECS

**Files:**
- Modify: `examples/basic_tilemap/main.cpp`
- Delete: (nothing — dungeon.hpp, tiles.hpp, font_gen.hpp stay the same)

Rewrite the roguelike demo to use the ECS World instead of the Game base class. The dungeon generation, tile definitions, and font generation stay the same. The game logic is split into systems.

**Step 1: Rewrite main.cpp**

Replace the contents of `examples/basic_tilemap/main.cpp` with:

```cpp
/// @file main.cpp
/// @brief Roguelike dungeon demo using ECS and the Angband Adam Bolt 16x16 tileset.
///
/// Generates a procedural dungeon with rooms and corridors. Move with WASD or
/// arrow keys. Walk over items to collect them. Bump into monsters to see their
/// name. Press R to regenerate the dungeon. Press Escape to quit.

#include <xebble/xebble.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "dungeon.hpp"
#include "font_gen.hpp"
#include "tiles.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <string>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

std::filesystem::path find_assets_dir() {
#ifdef __APPLE__
    char exe_buf[4096];
    uint32_t exe_size = sizeof(exe_buf);
    if (_NSGetExecutablePath(exe_buf, &exe_size) == 0) {
        auto resources = std::filesystem::path(exe_buf).parent_path() / "../Resources/assets";
        if (std::filesystem::exists(resources)) return resources;
    }
#endif
    return "examples/basic_tilemap/assets";
}

constexpr uint32_t TILE_SIZE = 16;
constexpr uint32_t VIRTUAL_W = 640;
constexpr uint32_t VIRTUAL_H = 360;
constexpr uint32_t VIEW_TILES_X = VIRTUAL_W / TILE_SIZE;
constexpr uint32_t VIEW_TILES_Y = VIRTUAL_H / TILE_SIZE;
constexpr uint32_t HUD_HEIGHT = 2;
constexpr uint32_t MAP_VIEW_TILES_Y = VIEW_TILES_Y - HUD_HEIGHT;

// --- ECS Components ---
struct PlayerTag {};
struct Position { int x, y; };
struct TileSprite { uint32_t tile_id; };
struct MonsterInfo { std::string name; };
struct ItemInfo { std::string name; };
struct Camera { int x, y; };
struct GameState {
    Dungeon dungeon;
    int items_collected = 0;
    std::string message = "Welcome to the dungeon. Use WASD or arrow keys to move.";
    bool needs_rebuild = true;
};

// --- Systems ---

class DungeonSystem : public xebble::System {
public:
    void init(xebble::World& world) override {
        rebuild(world);
    }

    void update(xebble::World& world, float) override {
        auto& state = world.resource<GameState>();
        if (!state.needs_rebuild) return;
        rebuild(world);
    }

private:
    void rebuild(xebble::World& world) {
        auto& state = world.resource<GameState>();
        state.dungeon = generate_dungeon();
        state.items_collected = 0;
        state.message = "Welcome to the dungeon. Use WASD or arrow keys to move.";
        state.needs_rebuild = false;

        // Clear old entities (brute force — destroy everything and recreate)
        std::vector<xebble::Entity> to_destroy;
        world.each<Position>([&](xebble::Entity e, Position&) {
            to_destroy.push_back(e);
        });
        for (auto e : to_destroy) world.destroy(e);
        world.flush_destroyed();

        auto& dg = state.dungeon;

        // Create player
        world.build_entity()
            .with<PlayerTag>({})
            .with<Position>({dg.player_start_x, dg.player_start_y})
            .with<TileSprite>({tiles::PLAYER})
            .build();

        // Create monsters
        for (auto& ent : dg.entities) {
            if (!ent.is_monster) continue;
            world.build_entity()
                .with<Position>({ent.x, ent.y})
                .with<TileSprite>({ent.tile})
                .with<MonsterInfo>({ent.name})
                .build();
        }

        // Create items
        for (auto& ent : dg.entities) {
            if (ent.is_monster) continue;
            world.build_entity()
                .with<Position>({ent.x, ent.y})
                .with<TileSprite>({ent.tile})
                .with<ItemInfo>({ent.name})
                .build();
        }

        // Update camera
        update_camera(world);
    }

    void update_camera(xebble::World& world) {
        auto& cam = world.resource<Camera>();
        world.each<PlayerTag, Position>([&](xebble::Entity, PlayerTag&, Position& pos) {
            cam.x = pos.x - static_cast<int>(VIEW_TILES_X) / 2;
            cam.y = pos.y - static_cast<int>(MAP_VIEW_TILES_Y) / 2;
            auto& dg = world.resource<GameState>().dungeon;
            cam.x = std::clamp(cam.x, 0, std::max(0, dg.width - static_cast<int>(VIEW_TILES_X)));
            cam.y = std::clamp(cam.y, 0, std::max(0, dg.height - static_cast<int>(MAP_VIEW_TILES_Y)));
        });
    }
};

class InputSystem : public xebble::System {
public:
    void update(xebble::World& world, float) override {
        auto& eq = world.resource<xebble::EventQueue>();
        for (auto& event : eq.events) {
            if (event.type != xebble::EventType::KeyPress) continue;
            switch (event.key().key) {
                case xebble::Key::W: case xebble::Key::Up:    try_move(world, 0, -1); break;
                case xebble::Key::S: case xebble::Key::Down:  try_move(world, 0,  1); break;
                case xebble::Key::A: case xebble::Key::Left:  try_move(world, -1, 0); break;
                case xebble::Key::D: case xebble::Key::Right: try_move(world,  1, 0); break;
                case xebble::Key::R: world.resource<GameState>().needs_rebuild = true; break;
                case xebble::Key::Escape: std::exit(0); break;
                default: break;
            }
        }
    }

private:
    void try_move(xebble::World& world, int dx, int dy) {
        auto& state = world.resource<GameState>();
        auto& cam = world.resource<Camera>();

        world.each<PlayerTag, Position>([&](xebble::Entity, PlayerTag&, Position& pos) {
            int nx = pos.x + dx;
            int ny = pos.y + dy;

            if (!state.dungeon.is_walkable(nx, ny)) {
                state.message = "You bump into a wall.";
                return;
            }

            // Check for monster
            bool blocked = false;
            world.each<MonsterInfo, Position>([&](xebble::Entity, MonsterInfo& info, Position& mpos) {
                if (mpos.x == nx && mpos.y == ny) {
                    state.message = std::format("You see a {}.", info.name);
                    blocked = true;
                }
            });
            if (blocked) return;

            pos.x = nx;
            pos.y = ny;

            // Check for item pickup
            bool picked_up = false;
            world.each<ItemInfo, Position>([&](xebble::Entity ie, ItemInfo& info, Position& ipos) {
                if (ipos.x == nx && ipos.y == ny) {
                    state.items_collected++;
                    state.message = std::format("You picked up {}.", info.name);
                    world.destroy(ie);
                    picked_up = true;
                }
            });

            if (!picked_up) {
                uint32_t feat = state.dungeon.feature_tiles[ny * state.dungeon.width + nx];
                if (feat == tiles::STAIRS_DOWN)
                    state.message = "You see a staircase leading down.";
                else if (feat == tiles::STAIRS_UP)
                    state.message = "You see a staircase leading up.";
                else if (feat == tiles::OPEN_DOOR)
                    state.message = "You pass through a doorway.";
                else
                    state.message = "";
            }

            // Update camera
            cam.x = pos.x - static_cast<int>(VIEW_TILES_X) / 2;
            cam.y = pos.y - static_cast<int>(MAP_VIEW_TILES_Y) / 2;
            cam.x = std::clamp(cam.x, 0, std::max(0, state.dungeon.width - static_cast<int>(VIEW_TILES_X)));
            cam.y = std::clamp(cam.y, 0, std::max(0, state.dungeon.height - static_cast<int>(MAP_VIEW_TILES_Y)));
        });
    }
};

class RenderSystem : public xebble::System {
    const xebble::SpriteSheet* sheet_ = nullptr;

public:
    void init(xebble::World& world) override {
        auto* assets = world.resource<xebble::AssetManager*>();
        sheet_ = &assets->get<xebble::SpriteSheet>("tiles");
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& state = world.resource<GameState>();
        auto& cam = world.resource<Camera>();
        auto& dg = state.dungeon;

        // Draw tilemap layers (floor + features)
        for (int layer = 0; layer < 2; layer++) {
            std::vector<xebble::SpriteInstance> instances;
            for (int vy = 0; vy <= static_cast<int>(MAP_VIEW_TILES_Y); vy++) {
                for (int vx = 0; vx <= static_cast<int>(VIEW_TILES_X); vx++) {
                    int tx = cam.x + vx;
                    int ty = cam.y + vy;
                    if (tx < 0 || tx >= dg.width || ty < 0 || ty >= dg.height) continue;
                    uint32_t tile;
                    if (layer == 0) {
                        tile = dg.floor_tiles[ty * dg.width + tx];
                    } else {
                        tile = dg.feature_tiles[ty * dg.width + tx];
                        if (tile == UINT32_MAX) continue;
                    }
                    auto uv = sheet_->region(tile);
                    instances.push_back({
                        .pos_x = static_cast<float>(vx * TILE_SIZE),
                        .pos_y = static_cast<float>(vy * TILE_SIZE),
                        .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                        .quad_w = static_cast<float>(TILE_SIZE),
                        .quad_h = static_cast<float>(TILE_SIZE),
                        .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                    });
                }
            }
            if (!instances.empty())
                renderer.submit_instances(instances, sheet_->texture(), static_cast<float>(layer));
        }

        // Draw entities (monsters + items)
        {
            std::vector<xebble::SpriteInstance> instances;
            world.each<Position, TileSprite>([&](xebble::Entity e, Position& pos, TileSprite& spr) {
                if (world.has<PlayerTag>(e)) return; // player drawn separately
                int sx = pos.x - cam.x;
                int sy = pos.y - cam.y;
                if (sx < 0 || sx >= static_cast<int>(VIEW_TILES_X) ||
                    sy < 0 || sy >= static_cast<int>(MAP_VIEW_TILES_Y))
                    return;
                auto uv = sheet_->region(spr.tile_id);
                instances.push_back({
                    .pos_x = static_cast<float>(sx * TILE_SIZE),
                    .pos_y = static_cast<float>(sy * TILE_SIZE),
                    .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                    .quad_w = static_cast<float>(TILE_SIZE),
                    .quad_h = static_cast<float>(TILE_SIZE),
                    .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                });
            });
            if (!instances.empty())
                renderer.submit_instances(instances, sheet_->texture(), 2.0f);
        }

        // Draw player
        world.each<PlayerTag, Position, TileSprite>([&](xebble::Entity, PlayerTag&, Position& pos, TileSprite& spr) {
            int px = pos.x - cam.x;
            int py = pos.y - cam.y;
            auto uv = sheet_->region(spr.tile_id);
            xebble::SpriteInstance inst{
                .pos_x = static_cast<float>(px * TILE_SIZE),
                .pos_y = static_cast<float>(py * TILE_SIZE),
                .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                .quad_w = static_cast<float>(TILE_SIZE),
                .quad_h = static_cast<float>(TILE_SIZE),
                .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            };
            renderer.submit_instances({&inst, 1}, sheet_->texture(), 3.0f);
        });
    }
};

class HudSystem : public xebble::System {
    const xebble::SpriteSheet* tile_sheet_ = nullptr;
    const xebble::BitmapFont* font_ = nullptr;

public:
    void init(xebble::World& world) override {
        auto* assets = world.resource<xebble::AssetManager*>();
        tile_sheet_ = &assets->get<xebble::SpriteSheet>("tiles");
        font_ = &assets->get<xebble::BitmapFont>("font");
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& state = world.resource<GameState>();
        float hud_y = static_cast<float>(MAP_VIEW_TILES_Y * TILE_SIZE);
        float gw = static_cast<float>(font_->glyph_width());
        float gh = static_cast<float>(font_->glyph_height());

        // HUD background
        {
            std::vector<xebble::SpriteInstance> hud_bg;
            auto wall_uv = tile_sheet_->region(tiles::PERMANENT_WALL);
            for (uint32_t x = 0; x < VIEW_TILES_X; x++) {
                for (uint32_t row = 0; row < HUD_HEIGHT; row++) {
                    hud_bg.push_back({
                        .pos_x = static_cast<float>(x * TILE_SIZE),
                        .pos_y = static_cast<float>(MAP_VIEW_TILES_Y * TILE_SIZE + row * TILE_SIZE),
                        .uv_x = wall_uv.x, .uv_y = wall_uv.y,
                        .uv_w = wall_uv.w, .uv_h = wall_uv.h,
                        .quad_w = static_cast<float>(TILE_SIZE),
                        .quad_h = static_cast<float>(TILE_SIZE),
                        .r = 0.2f, .g = 0.2f, .b = 0.25f, .a = 1.0f,
                    });
                }
            }
            renderer.submit_instances(hud_bg, tile_sheet_->texture(), 4.0f);
        }

        // HUD text
        int player_x = 0, player_y = 0;
        world.each<PlayerTag, Position>([&](xebble::Entity, PlayerTag&, Position& pos) {
            player_x = pos.x;
            player_y = pos.y;
        });

        auto draw_text = [&](const std::string& text, float x, float y,
                              float r, float g, float b) {
            std::vector<xebble::SpriteInstance> glyphs;
            for (size_t i = 0; i < text.size(); i++) {
                auto gi = font_->glyph_index(text[i]);
                if (!gi) continue;
                auto uv = font_->sheet().region(*gi);
                glyphs.push_back({
                    .pos_x = x + static_cast<float>(i) * gw,
                    .pos_y = y,
                    .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                    .quad_w = gw, .quad_h = gh,
                    .r = r, .g = g, .b = b, .a = 1.0f,
                });
            }
            if (!glyphs.empty())
                renderer.submit_instances(glyphs, font_->sheet().texture(), 5.0f);
        };

        auto status = std::format("Pos:({},{}) Items:{} [R]egen [Esc]ape",
                                   player_x, player_y, state.items_collected);
        draw_text(status, 4.0f, hud_y + 4.0f, 1.0f, 1.0f, 0.6f);

        if (!state.message.empty())
            draw_text(state.message, 4.0f, hud_y + 4.0f + gh + 2.0f, 0.8f, 0.8f, 0.8f);
    }
};

} // namespace

int main() {
    auto assets_dir = find_assets_dir();
    std::filesystem::create_directories(assets_dir);

    // Generate bitmap font atlas
    font_gen::generate_font(assets_dir / "font.png");

    // Write manifest
    auto manifest_path = assets_dir / "manifest.toml";
    {
        auto charset = font_gen::font_charset();
        std::string escaped;
        for (char c : charset) {
            if (c == '\\') escaped += "\\\\";
            else if (c == '"') escaped += "\\\"";
            else escaped += c;
        }

        std::FILE* f = std::fopen(manifest_path.string().c_str(), "w");
        if (f) {
            std::fprintf(f,
                "[spritesheets.tiles]\n"
                "path = \"angband-16x16.png\"\n"
                "tile_width = 16\n"
                "tile_height = 16\n"
                "\n"
                "[bitmap_fonts.font]\n"
                "path = \"font.png\"\n"
                "glyph_width = %d\n"
                "glyph_height = %d\n"
                "charset = \"%s\"\n",
                font_gen::GLYPH_W, font_gen::GLYPH_H, escaped.c_str());
            std::fclose(f);
        }
    }

    xebble::World world;

    // Register components
    world.register_component<PlayerTag>();
    world.register_component<Position>();
    world.register_component<TileSprite>();
    world.register_component<MonsterInfo>();
    world.register_component<ItemInfo>();

    // Add resources
    world.add_resource<Camera>({0, 0});
    world.add_resource<GameState>({});

    // Add systems (order matters)
    world.add_system<DungeonSystem>();
    world.add_system<InputSystem>();
    world.add_system<RenderSystem>();
    world.add_system<HudSystem>();

    return xebble::run(std::move(world), {
        .window = {.title = "Xebble - Roguelike Demo", .width = 1280, .height = 720},
        .renderer = {.virtual_width = VIRTUAL_W, .virtual_height = VIRTUAL_H},
        .assets = {.directory = assets_dir, .manifest = manifest_path},
    });
}
```

**Step 2: Build and test**

Run: `cmake --build build/debug --target basic_tilemap 2>&1`
Expected: Compiles and links.

Run the app manually to verify it works: WASD movement, item pickup, monster bumping, R to regenerate, HUD displays correctly.

**Step 3: Commit**

```bash
git add examples/basic_tilemap/main.cpp
git commit -m "refactor: rewrite roguelike demo to use ECS World and Systems"
```

---

### Task 8: Remove Game Base Class

**Files:**
- Delete: `include/xebble/game.hpp` (replace with new version)
- Modify: `src/game.cpp`
- Modify: `include/xebble/xebble.hpp`

Now that no code uses the Game base class, remove it. Keep `GameConfig` and the World-based `run()`.

**Step 1: Rewrite game.hpp**

Replace `include/xebble/game.hpp` with:

```cpp
/// @file game.hpp
/// @brief Game configuration and run function.
#pragma once

#include <xebble/types.hpp>
#include <xebble/window.hpp>
#include <xebble/renderer.hpp>
#include <xebble/asset_manager.hpp>
#include <xebble/world.hpp>

namespace xebble {

/// @brief Configuration for the game loop.
struct GameConfig {
    WindowConfig window;
    RendererConfig renderer;
    AssetConfig assets;
    float fixed_timestep = 1.0f / 60.0f;
};

/// @brief Create systems and run the game loop with an ECS World.
/// @param world The ECS world with registered components and systems.
/// @param config Full game configuration.
/// @return 0 on success, non-zero on failure.
int run(World world, const GameConfig& config);

} // namespace xebble
```

**Step 2: Remove old Game-based run() from game.cpp**

Rewrite `src/game.cpp` to only contain the World-based `run()`:

```cpp
/// @file game.cpp
/// @brief Game loop implementation.
#include <xebble/game.hpp>
#include <xebble/event.hpp>
#include <xebble/log.hpp>

#include <span>

namespace xebble {

int run(World world, const GameConfig& config) {
    auto window = Window::create(config.window);
    if (!window) {
        log(LogLevel::Error, "Failed to create window: " + window.error().message);
        return 1;
    }

    auto renderer = Renderer::create(*window, config.renderer);
    if (!renderer) {
        log(LogLevel::Error, "Failed to create renderer: " + renderer.error().message);
        return 1;
    }

    auto assets = AssetManager::create(renderer->context(), config.assets);
    if (!assets) {
        log(LogLevel::Error, "Failed to create asset manager: " + assets.error().message);
        return 1;
    }

    world.add_resource<Renderer*>(&*renderer);
    world.add_resource<AssetManager*>(&*assets);
    world.add_resource<EventQueue>(EventQueue{});

    world.init_systems();

    float accumulator = 0.0f;

    while (!window->should_close()) {
        window->poll_events();
        world.resource<EventQueue>().events = window->events();

        accumulator += renderer->delta_time();
        while (accumulator >= config.fixed_timestep) {
            world.tick_update(config.fixed_timestep);
            accumulator -= config.fixed_timestep;
        }

        if (renderer->begin_frame()) {
            world.tick_draw(*renderer);
            renderer->end_frame();
        }
    }

    return 0;
}

} // namespace xebble
```

**Step 3: Build and run all tests**

Run: `cmake --build build/debug && ctest --test-dir build/debug`
Expected: All tests pass.

Run: `cmake --build build/debug --target basic_tilemap` and run the app.
Expected: Demo works identically.

**Step 4: Commit**

```bash
git add include/xebble/game.hpp src/game.cpp
git commit -m "refactor: remove Game base class, keep only World-based run()"
```
