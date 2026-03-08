# Xebble

A 2D Vulkan pixel-art game engine written in C++23, targeting roguelikes and retro-style games.

Xebble provides a minimal, opinionated framework: an ECS with sparse-set storage, batched sprite rendering via MoltenVK/Vulkan, tile maps, bitmap fonts, UI panels, field-of-view, pathfinding, procedural generation, and serialization — all wired together in a single `xebble::run()` call.

## Features

- **ECS** — Sparse-set component pools, per-entity bitmask filtering, multi-component queries, deferred destruction, fluent entity builder
- **Sprite Rendering** — Instanced batched draw calls, sorted by z-order and texture, with direct GPU memory writes and incremental position patching
- **Tile Maps** — Multi-layer tile map rendering from sprite sheets
- **Bitmap Fonts** — Embedded proportional fonts with UTF-8 support
- **UI** — Immediate-mode panel/text system with anchoring and theming
- **Grid Utilities** — 2D grid, Bresenham FOV, A* pathfinding
- **Procedural Generation** — BSP dungeon generator, cellular automata caves
- **Serialization** — Binary snapshot/restore for save games (trivially-copyable components)
- **Scene Stack** — Push/pop scene management with per-scene ECS worlds
- **RNG** — Seedable xoshiro256++ random number generator
- **Turn System** — Energy-based turn scheduling

## Requirements

- **macOS** with Apple Silicon (Linux/Windows stubs exist but are not yet implemented)
- **C++23** compiler (Clang 16+ / Apple Clang 15+)
- **CMake** 3.25+
- **vcpkg** — install at `~/vcpkg` (or set `VCPKG_ROOT`)

## Building

```bash
# Configure and build (debug)
cmake --preset debug
cmake --build build/debug

# Run tests
ctest --preset debug
```

### Dependencies (managed by vcpkg)

glfw3, vulkan-headers, vulkan-loader, vulkan-memory-allocator, stb, freetype, shaderc, glm, tomlplusplus, minizip-ng, gtest

## Examples

| Example | Description |
|---|---|
| `ex01_hello_world` | Minimal window + renderer setup |
| `ex02_tilemap` | Tile map rendering |
| `ex03_sprites` | Sprite batching, boid simulation |
| `ex04_input` | Keyboard and mouse input handling |
| `ex05_ui` | Immediate-mode UI panels |
| `ex06_ecs` | ECS deep dive: entities, components, systems, resources |
| `ex07_scene` | Scene stack transitions |
| `ex08_grid_fov_path` | Grid, field-of-view, A* pathfinding |
| `ex09_procgen` | BSP dungeon + cellular automata caves |
| `ex10_rng` | Seedable RNG distributions |
| `ex11_turn` | Energy-based turn scheduling |
| `ex12_msglog` | Scrolling message log |
| `ex13_serial` | Save/load via snapshot/restore |
| `ex14_fonts` | Embedded bitmap fonts + UTF-8 |
| `ex15_atlas_debug` | Texture atlas visualization |

Run an example (macOS):

```bash
open build/debug/examples/ex06_ecs.app
```

## Quick Start

```cpp
#include <xebble/xebble.hpp>

class MyGame : public xebble::System {
public:
    void init(xebble::World& world) override {
        auto* renderer = world.resource<xebble::Renderer*>();
        // Load assets, spawn entities...
    }

    void update(xebble::World& world, float dt) override {
        // Game logic — dt is elapsed time in seconds
        world.each<xebble::Position, Velocity>([&](auto, auto& pos, auto& vel) {
            pos.x += vel.dx * dt;
            pos.y += vel.dy * dt;
        });
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        // UI, debug overlays, etc.
    }
};

int main() {
    xebble::World world;
    world.register_component<Velocity>();
    world.add_system<MyGame>();

    return xebble::run(std::move(world), {
        .window   = {.title = "My Game", .width = 1280, .height = 720},
        .renderer = {.virtual_width = 640, .virtual_height = 360},
    });
}
```

## Project Structure

```
include/xebble/   — Public headers
src/               — Engine implementation
src/vulkan/        — Vulkan/VMA wrappers (buffer, swapchain, context, pipeline)
shaders/           — GLSL vertex/fragment shaders (compiled at build time via shaderc)
examples/          — Runnable example programs
tests/             — Google Test suite (315 tests)
fonts/             — Embedded font data
docs/              — Design documents
```

## Code Style

- C++23 with C++20 baseline compatibility
- RAII everywhere, value semantics preferred
- No raw pointers in public API
- `std::expected<T, Error>` for fallible construction
- Move-only types for GPU resource owners

## License

See LICENSE file.
