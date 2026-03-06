# Built-in Rendering Design

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:writing-plans to create implementation plan from this design.

**Goal:** Move common rendering responsibilities into the xebble framework so games don't need custom render systems for sprites and tilemaps.

**Architecture:** Built-in ECS components (Position, Sprite, TileMapLayer) and a Camera resource. Framework provides TileMapRenderSystem and SpriteRenderSystem, auto-registered by run() after user systems.

---

## Built-in Components

### Position

World coordinates in pixels. Float for future smooth movement/tweening.

```cpp
struct Position { float x, y; };
```

### Sprite

What to draw for an entity. References a spritesheet and tile index.

```cpp
struct Sprite {
    const SpriteSheet* sheet;
    uint32_t tile_index;
    float z_order = 0.0f;
    Color tint = {255, 255, 255, 255};
};
```

### TileMapLayer

Attaches a tilemap to an entity for rendering. Uses shared_ptr for safe ownership across dungeon rebuilds.

```cpp
struct TileMapLayer {
    std::shared_ptr<TileMap> tilemap;
    float z_order = 0.0f;
};
```

---

## Built-in Resource

### Camera

Top-left corner of the viewport in world pixels.

```cpp
struct Camera { float x = 0, y = 0; };
```

---

## Built-in Systems

Added automatically by `run()` after all user systems, so user update logic always runs before framework rendering.

### TileMapRenderSystem

- Iterates all entities with `TileMapLayer`
- For each, draws visible tiles within the viewport, offset by Camera position
- Sorted by z_order (lower draws first)
- Uses the tilemap's associated SpriteSheet for UV lookup

### SpriteRenderSystem

- Iterates all entities with `Position` + `Sprite`
- Calculates screen position by subtracting Camera offset
- Culls off-screen entities
- Batches by texture for efficient submission
- Sorted by z_order (lower draws first)

---

## Auto-registration in run()

`run()` automatically performs these steps before calling `init_systems()`:

1. Registers built-in component types: `Position`, `Sprite`, `TileMapLayer`
2. Adds `Camera` resource with default position (0, 0)
3. Appends `TileMapRenderSystem` and `SpriteRenderSystem` after user systems

User systems always run before built-in render systems in both update and draw phases.

---

## Example Impact

The roguelike demo removes its custom RenderSystem entirely. DungeonSystem creates entities with `xebble::Position` + `xebble::Sprite` and tilemap entities with `xebble::TileMapLayer`. InputSystem updates `xebble::Camera`. HudSystem draws text overlay (custom, not entity-based). The framework handles all sprite and tilemap rendering.

---

## Files

New/modified headers:
- `include/xebble/components.hpp` — Position, Sprite, TileMapLayer, Camera
- `include/xebble/builtin_systems.hpp` — TileMapRenderSystem, SpriteRenderSystem

New source:
- `src/builtin_systems.cpp` — render system implementations

Modified:
- `src/game.cpp` — auto-register components, resource, and systems in run()
- `include/xebble/xebble.hpp` — add new headers to umbrella
- `examples/basic_tilemap/main.cpp` — remove RenderSystem, use built-in components
