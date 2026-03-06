# ECS (Entity Component System) Design

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:writing-plans to create implementation plan from this design.

**Goal:** Add a first-class ECS to xebble, replacing the Game base class as the primary way to build games.

**Architecture:** Sparse-set-backed component storage with generational entity IDs, singleton resources, and systems with init/update/draw lifecycle. The World is the central coordinator; `xebble::run()` accepts a World instead of a Game subclass.

---

## Entity

A 32-bit generational index. Lower bits encode a slot index, upper bits encode a generation counter. When an entity is destroyed, its slot is recycled; the generation increments so stale handles are detected.

```cpp
struct Entity {
    uint32_t id;  // index + generation packed
    bool operator==(Entity other) const { return id == other.id; }
};
```

Users never construct Entity directly — only the World creates them.

---

## Components

Components are pure data structs (PODs). No behavior, no inheritance. Examples:

```cpp
struct Position { int x, y; };
struct TileSprite { uint32_t tile_id; };
struct Health { int current, max; };
```

### Storage: Sparse Set

Each component type gets a `ComponentPool<T>` backed by a sparse set:

- **Sparse array** — indexed by entity slot index, holds position into dense array (or sentinel for "not present")
- **Dense array** — packed contiguous `T` values + parallel entity ID array for reverse lookup

Properties: O(1) add/remove/has/get, cache-friendly iteration over the dense array.

### Type Erasure

A non-templated `IComponentPool` base enables the World to manage heterogeneous pools:

```cpp
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(Entity e) = 0;
    virtual bool has(Entity e) const = 0;
};

template<typename T>
class ComponentPool : public IComponentPool { /* sparse set */ };
```

### Type ID

Static counter per type:

```cpp
using ComponentId = uint32_t;

template<typename T>
ComponentId component_id() {
    static ComponentId id = next_id();
    return id;
}
```

World holds `std::vector<std::unique_ptr<IComponentPool>>` indexed by ComponentId.

---

## World

Central coordinator. Owns entity allocator, component pools, systems, and resources.

```cpp
class World {
public:
    // Entity lifecycle
    Entity create_entity();
    EntityBuilder build_entity();
    void destroy(Entity e);              // deferred until end of tick
    bool alive(Entity e) const;

    // Components
    template<typename T> void register_component();
    template<typename T> T& add(Entity e, T value);
    template<typename T> void remove(Entity e);
    template<typename T> T& get(Entity e);
    template<typename T> const T& get(Entity e) const;
    template<typename T> bool has(Entity e) const;

    // Iteration
    template<typename... Ts>
    void each(std::function<void(Entity, Ts&...)> fn);

    // Resources (singleton components)
    template<typename T> void add_resource(T value);
    template<typename T> void remove_resource();
    template<typename T> T& resource();
    template<typename T> const T& resource() const;
    template<typename T> bool has_resource() const;

    // Systems
    template<typename T, typename... Args>
    void add_system(Args&&... args);

    // Called by xebble::run() — not user-facing
    void init_systems();
    void tick_update(float dt);
    void tick_draw(Renderer& renderer);
};
```

### Deferred Destruction

`destroy(Entity)` marks the entity for removal. Actual cleanup (removing all components, recycling the slot) happens at the end of `tick_update()`, after all update systems have run but before draw systems.

### Entity Builder

Fluent API for creating entities with multiple components:

```cpp
class EntityBuilder {
public:
    template<typename T> EntityBuilder& with(T value);
    Entity build();
};
```

Usage:

```cpp
auto e = world.build_entity()
    .with<Position>({10, 20})
    .with<TileSprite>({tiles::KOBOLD})
    .with<Health>({15, 15})
    .build();
```

### Iteration

Systems query entities by component combination:

```cpp
world.each<Position, TileSprite>([&](Entity e, Position& pos, TileSprite& spr) {
    // runs for every entity that has both Position and TileSprite
});
```

Implementation: iterate the smallest ComponentPool's dense array, filter by `has<T>()` for the other types.

---

## Resources

Singleton values with no entity. Stored in `std::unordered_map<ComponentId, std::any>` — iteration performance doesn't matter, only lookup.

Engine-provided resources (added by `run()` before `init()` is called):
- `Renderer&` — reference to the renderer
- `AssetManager&` — reference to the asset manager
- `EventQueue` — input events for the current frame

User-defined resources:
- Game state (dungeon data, score, turn counter, etc.)

```cpp
struct EventQueue {
    std::span<const Event> events;
};
```

---

## Systems

Systems implement behavior. They override one or more lifecycle methods:

```cpp
class System {
public:
    virtual ~System() = default;
    virtual void init(World& world) {}
    virtual void update(World& world, float dt) {}
    virtual void draw(World& world, Renderer& renderer) {}
};
```

Systems run in registration order. `run()` calls:
1. `init()` for all systems (once, after resources are available)
2. Each frame: `update()` for all systems, flush deferred destroys, then `draw()` for all systems

Systems declare no dependencies — they query what they need at runtime via `world.each<>()` and `world.resource<>()`.

### Roguelike System Pipeline Example

```cpp
world.add_system<InputSystem>();    // captures player action
world.add_system<TurnSystem>();     // if player acted, run AI/combat
world.add_system<CameraSystem>();   // update camera follow
world.add_system<RenderSystem>();   // draw map + entities
world.add_system<HudSystem>();      // draw UI overlay
```

---

## Integration with xebble::run()

The `Game` base class is removed. `run()` accepts a World:

```cpp
int run(World world, const GameConfig& config);
```

Internally:
1. Creates Window, Renderer, AssetManager
2. Adds them as resources on the World
3. Calls `world.init_systems()`
4. Enters fixed-timestep loop:
   - Poll events, update `EventQueue` resource
   - Call `world.tick_update(dt)`
   - Call `world.tick_draw(renderer)`
5. Cleanup on exit

**GameConfig** stays the same (WindowConfig, RendererConfig, AssetConfig, fixed_timestep).

### Complete Program Example

```cpp
int main() {
    xebble::World world;

    world.register_component<Position>();
    world.register_component<TileSprite>();
    world.register_component<Health>();

    world.add_system<InputSystem>();
    world.add_system<TurnSystem>();
    world.add_system<CameraSystem>();
    world.add_system<RenderSystem>();
    world.add_system<HudSystem>();

    return xebble::run(std::move(world), {
        .window = {.title = "My Roguelike", .width = 1280, .height = 720},
        .renderer = {.virtual_width = 640, .virtual_height = 360},
        .assets = {.directory = "assets", .manifest = "assets/manifest.toml"},
    });
}
```

---

## Files

New headers:
- `include/xebble/ecs.hpp` — Entity, ComponentPool, IComponentPool, component_id
- `include/xebble/world.hpp` — World, EntityBuilder
- `include/xebble/system.hpp` — System base class

New source:
- `src/world.cpp` — World non-template implementation

Modified:
- `include/xebble/xebble.hpp` — add new headers to umbrella
- `src/game_loop.cpp` — change `run()` to accept World instead of Game
- `include/xebble/game.hpp` — remove (or keep deprecated)
- `examples/basic_tilemap/` — rewrite to use ECS
