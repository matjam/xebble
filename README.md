# Xebble

A 2D Vulkan pixel-art game engine written in C++23, targeting roguelikes and retro-style games.

Xebble provides a minimal, opinionated framework: an ECS with sparse-set storage, batched sprite rendering via Vulkan, tile maps, bitmap fonts, immediate-mode UI, field-of-view, pathfinding, procedural generation, serialization, audio, and scene management -- all wired together in a single `xebble::run()` call.

## Features

- **ECS** -- Sparse-set component pools, per-entity bitmask filtering, multi-component queries, deferred destruction, fluent entity builder
- **Sprite Rendering** -- Instanced batched draw calls, sorted by z-order and texture, with direct GPU memory writes and incremental position patching
- **Tile Maps** -- Multi-layer tile map rendering from sprite sheets
- **Bitmap Fonts** -- Embedded proportional fonts (PetMe64, PetMe642Y, Berkelium64) with UTF-8 support, plus FreeType-based font loading
- **UI** -- Immediate-mode panel/widget system with anchoring, theming, buttons, checkboxes, sliders, dropdowns, progress bars, separators, and message logs
- **Grid Utilities** -- 2D grid, Bresenham FOV, A* pathfinding
- **Procedural Generation** -- BSP dungeon generator, cellular automata caves
- **Serialization** -- Binary snapshot/restore for save games with custom serialization hooks for non-trivially-copyable components
- **Scene Stack** -- Push/pop scene management with per-scene ECS worlds
- **Audio** -- miniaudio playback engine with libxmp tracker music support and libsidplayfp SID chiptune emulation
- **RNG** -- Seedable PCG32 random number generator with dice notation, weighted tables, and Fisher-Yates shuffle
- **Turn System** -- Energy-based turn scheduling
- **Input Mapping** -- Named action-to-key binding layer with TOML-based configuration
- **Asset Manager** -- TOML manifest-driven asset loading with optional ZIP archive support

## Platforms

- **Linux** (primary) -- Vulkan via system loader, GLFW with Wayland support
- **macOS** -- Vulkan via MoltenVK, bundled into `.app` bundles automatically
- **Windows** -- Stubs exist but are not yet implemented

## Requirements

- **C++23** compiler (GCC 13+, Clang 16+, Apple Clang 15+)
- **CMake** 4.0+
- **vcpkg** -- set `VCPKG_ROOT` environment variable (e.g. `~/.local/share/vcpkg`)
- **Vulkan SDK** or system Vulkan loader

## Building

```bash
# Configure and build (debug)
cmake --preset debug
cmake --build build/debug

# Run tests
ctest --preset debug
```

### Dependencies (managed by vcpkg)

glfw3, vulkan-headers, vulkan-loader, vulkan-memory-allocator, stb, freetype, shaderc, glm, tomlplusplus, miniz, miniaudio, libxmp, gtest

### Vendored dependencies

- **libsidplayfp 2.16.1** (GPL-2.0-or-later) -- SID chip emulation for chiptune audio

## Installing as a library

Xebble can be installed and consumed by other CMake projects via `find_package()`:

```bash
# Build and install to a local prefix
cmake --preset release
cmake --build build/release
cmake --install build/release --prefix ~/.local
```

### Consuming from another project

In your game's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 4.0)
project(my_game LANGUAGES CXX)

find_package(xebble REQUIRED)

add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE xebble::xebble)
```

Your game project will also need vcpkg for the transitive dependencies. Set the
same `CMAKE_TOOLCHAIN_FILE` in your presets or command line. The `xebbleConfig.cmake`
package file calls `find_dependency()` for all required packages automatically.

The installed package also exports `XEBBLE_SHADER_DIR` pointing to the installed
SPIR-V shaders, though the renderer searches for them automatically relative to
the executable.

## Examples

| Example | Description |
|---|---|
| `ex01_hello_world` | Minimal window + renderer setup |
| `ex02_tilemap` | Tile map rendering |
| `ex03_sprites` | Sprite batching, boid simulation |
| `ex04_input` | Keyboard and mouse input handling |
| `ex05_ui` | Immediate-mode UI panels and widgets |
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
| `ex16_graphics_settings` | V-sync and display settings |
| `ex17_audio` | Audio engine with SFX, tracker music, and SID playback |

Run an example:

```bash
# Linux
./build/debug/examples/ex06_ecs

# macOS
open build/debug/examples/ex06_ecs.app
```

## Quick Start

```cpp
#include <xebble/xebble.hpp>

struct Velocity {
    float dx, dy;
};

class MyGame : public xebble::System {
public:
    void init(xebble::World& world) override {
        // Load assets, spawn entities...
    }

    void update(xebble::World& world, float dt) override {
        // Game logic -- dt is elapsed time in seconds
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
include/xebble/   -- Public headers (32 headers)
src/               -- Engine implementation
src/vulkan/        -- Vulkan/VMA wrappers (buffer, swapchain, context, pipeline)
src/sidplay/       -- Vendored libsidplayfp (SID chip emulator)
shaders/           -- GLSL vertex/fragment shaders (compiled at build time via shaderc)
examples/          -- 17 runnable example programs
tests/             -- Google Test suite (358 tests)
fonts/             -- Embedded font data
docs/              -- Design documents
cmake/             -- CMake package config template
```

## Code Style

- C++23 with C++20 baseline compatibility
- RAII everywhere, value semantics preferred
- No raw pointers in public API
- `std::expected<T, Error>` for fallible construction
- Move-only types for GPU resource owners
- `[[nodiscard]]` on all public getters, query functions, and factory methods

## License

See LICENSE file. Note that the vendored libsidplayfp is licensed under GPL-2.0-or-later.
