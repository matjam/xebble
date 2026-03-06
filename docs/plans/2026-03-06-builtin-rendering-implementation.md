# Built-in Rendering Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Move common rendering into the xebble framework with built-in components (Position, Sprite, TileMapLayer), a Camera resource, and auto-registered render systems, so games need zero custom render code for sprites and tilemaps.

**Architecture:** New `components.hpp` header defines built-in ECS components. New `builtin_systems.cpp` implements TileMapRenderSystem and SpriteRenderSystem. `run()` auto-registers these components, the Camera resource, and the render systems after user systems.

**Tech Stack:** C++23, GoogleTest, CMake

---

### Task 1: Built-in Components Header

**Files:**
- Create: `include/xebble/components.hpp`
- Modify: `include/xebble/xebble.hpp`

Create the built-in component types and Camera resource.

**Step 1: Create the header**

Create `include/xebble/components.hpp`:

```cpp
/// @file components.hpp
/// @brief Built-in ECS components and resources provided by xebble.
#pragma once

#include <xebble/types.hpp>
#include <xebble/tilemap.hpp>

#include <memory>

namespace xebble {

class SpriteSheet;

/// @brief World-space position in pixels.
struct Position {
    float x = 0;
    float y = 0;
};

/// @brief Sprite rendering component. Attach to an entity with Position to draw it.
struct Sprite {
    const SpriteSheet* sheet = nullptr;
    uint32_t tile_index = 0;
    float z_order = 0.0f;
    Color tint = {255, 255, 255, 255};
};

/// @brief TileMap rendering component. Attach to an entity to draw a tilemap layer.
struct TileMapLayer {
    std::shared_ptr<TileMap> tilemap;
    float z_order = 0.0f;
};

/// @brief Camera resource. Top-left corner of the viewport in world pixels.
struct Camera {
    float x = 0;
    float y = 0;
};

} // namespace xebble
```

**Step 2: Add to umbrella header**

Modify `include/xebble/xebble.hpp` — add `#include <xebble/components.hpp>` after the `#include <xebble/world.hpp>` line.

**Step 3: Verify it compiles**

Run: `cmake --build build/debug 2>&1 | tail -5`
Expected: Compiles cleanly.

**Step 4: Commit**

```bash
git add include/xebble/components.hpp include/xebble/xebble.hpp
git commit -m "feat: add built-in Position, Sprite, TileMapLayer components and Camera resource

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

### Task 2: Built-in Render Systems

**Files:**
- Create: `include/xebble/builtin_systems.hpp`
- Create: `src/builtin_systems.cpp`
- Modify: `src/CMakeLists.txt`

Implement TileMapRenderSystem and SpriteRenderSystem.

**Step 1: Create the header**

Create `include/xebble/builtin_systems.hpp`:

```cpp
/// @file builtin_systems.hpp
/// @brief Built-in ECS systems provided by xebble.
#pragma once

#include <xebble/system.hpp>

namespace xebble {

/// @brief Renders all entities with TileMapLayer, offset by Camera.
class TileMapRenderSystem : public System {
public:
    void draw(World& world, Renderer& renderer) override;
};

/// @brief Renders all entities with Position + Sprite, offset by Camera.
class SpriteRenderSystem : public System {
public:
    void draw(World& world, Renderer& renderer) override;
};

} // namespace xebble
```

**Step 2: Create the implementation**

Create `src/builtin_systems.cpp`:

```cpp
/// @file builtin_systems.cpp
/// @brief Built-in render system implementations.
#include <xebble/builtin_systems.hpp>
#include <xebble/components.hpp>
#include <xebble/world.hpp>
#include <xebble/renderer.hpp>
#include <xebble/spritesheet.hpp>
#include <xebble/tilemap.hpp>

#include <algorithm>
#include <vector>

namespace xebble {

void TileMapRenderSystem::draw(World& world, Renderer& renderer) {
    auto& cam = world.resource<Camera>();
    uint32_t vw = renderer.virtual_width();
    uint32_t vh = renderer.virtual_height();

    // Collect and sort tilemap layers by z_order
    struct TileMapEntry {
        TileMapLayer* layer;
    };
    std::vector<TileMapEntry> entries;
    world.each<TileMapLayer>([&](Entity, TileMapLayer& tl) {
        if (tl.tilemap) entries.push_back({&tl});
    });
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.layer->z_order < b.layer->z_order; });

    for (auto& entry : entries) {
        auto& tm = *entry.layer->tilemap;
        auto& sheet = tm.sheet();
        uint32_t tw = sheet.tile_width();
        uint32_t th = sheet.tile_height();

        // Calculate visible tile range
        int start_tx = static_cast<int>(cam.x) / static_cast<int>(tw);
        int start_ty = static_cast<int>(cam.y) / static_cast<int>(th);
        int end_tx = start_tx + static_cast<int>(vw / tw) + 1;
        int end_ty = start_ty + static_cast<int>(vh / th) + 1;

        start_tx = std::max(start_tx, 0);
        start_ty = std::max(start_ty, 0);
        end_tx = std::min(end_tx, static_cast<int>(tm.width()));
        end_ty = std::min(end_ty, static_cast<int>(tm.height()));

        for (uint32_t layer = 0; layer < tm.layer_count(); layer++) {
            std::vector<SpriteInstance> instances;
            for (int ty = start_ty; ty < end_ty; ty++) {
                for (int tx = start_tx; tx < end_tx; tx++) {
                    auto tile = tm.tile_at(layer, static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                    if (!tile) continue;
                    auto uv = sheet.region(*tile);
                    float screen_x = static_cast<float>(tx) * static_cast<float>(tw) - cam.x;
                    float screen_y = static_cast<float>(ty) * static_cast<float>(th) - cam.y;
                    instances.push_back({
                        .pos_x = screen_x,
                        .pos_y = screen_y,
                        .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                        .quad_w = static_cast<float>(tw),
                        .quad_h = static_cast<float>(th),
                        .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                    });
                }
            }
            if (!instances.empty()) {
                // Use tilemap z_order as base, add small offset per layer for internal ordering
                renderer.submit_instances(instances, sheet.texture(),
                                          entry.layer->z_order + static_cast<float>(layer) * 0.01f);
            }
        }
    }
}

void SpriteRenderSystem::draw(World& world, Renderer& renderer) {
    auto& cam = world.resource<Camera>();
    uint32_t vw = renderer.virtual_width();
    uint32_t vh = renderer.virtual_height();

    // Collect visible sprites
    struct SpriteEntry {
        float screen_x, screen_y;
        const SpriteSheet* sheet;
        uint32_t tile_index;
        float z_order;
        Color tint;
    };
    std::vector<SpriteEntry> entries;

    world.each<Position, Sprite>([&](Entity, Position& pos, Sprite& spr) {
        if (!spr.sheet) return;
        float sx = pos.x - cam.x;
        float sy = pos.y - cam.y;
        float tw = static_cast<float>(spr.sheet->tile_width());
        float th = static_cast<float>(spr.sheet->tile_height());
        // Cull off-screen
        if (sx + tw < 0 || sx > static_cast<float>(vw) ||
            sy + th < 0 || sy > static_cast<float>(vh))
            return;
        entries.push_back({sx, sy, spr.sheet, spr.tile_index, spr.z_order, spr.tint});
    });

    // Sort by z_order
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.z_order < b.z_order; });

    // Batch by texture and z_order
    // Simple approach: submit each z_order group per texture
    for (auto& e : entries) {
        auto uv = e.sheet->region(e.tile_index);
        float tw = static_cast<float>(e.sheet->tile_width());
        float th = static_cast<float>(e.sheet->tile_height());
        SpriteInstance inst{
            .pos_x = e.screen_x,
            .pos_y = e.screen_y,
            .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
            .quad_w = tw, .quad_h = th,
            .r = static_cast<float>(e.tint.r) / 255.0f,
            .g = static_cast<float>(e.tint.g) / 255.0f,
            .b = static_cast<float>(e.tint.b) / 255.0f,
            .a = static_cast<float>(e.tint.a) / 255.0f,
        };
        renderer.submit_instances({&inst, 1}, e.sheet->texture(), e.z_order);
    }
}

} // namespace xebble
```

**Step 3: Add to CMake**

Modify `src/CMakeLists.txt` — add `builtin_systems.cpp` after `world.cpp` in the source list.

**Step 4: Add to umbrella header**

Modify `include/xebble/xebble.hpp` — add `#include <xebble/builtin_systems.hpp>` after `#include <xebble/components.hpp>`.

**Step 5: Verify it compiles**

Run: `cmake --build build/debug 2>&1 | tail -5`
Expected: Compiles cleanly.

**Step 6: Commit**

```bash
git add include/xebble/builtin_systems.hpp src/builtin_systems.cpp src/CMakeLists.txt include/xebble/xebble.hpp
git commit -m "feat: add TileMapRenderSystem and SpriteRenderSystem built-in systems

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

### Task 3: Auto-register Built-ins in run()

**Files:**
- Modify: `src/game.cpp`

Update `run()` to automatically register built-in components, add Camera resource, and append built-in render systems.

**Step 1: Update run()**

Modify `src/game.cpp`. Add includes at the top:

```cpp
#include <xebble/components.hpp>
#include <xebble/builtin_systems.hpp>
```

In the `run()` function, after `world.add_resource<EventQueue>(EventQueue{});` (line 31) and before `world.init_systems();` (line 33), add:

```cpp
    // Register built-in components
    world.register_component<Position>();
    world.register_component<Sprite>();
    world.register_component<TileMapLayer>();

    // Add default Camera resource if not already provided
    if (!world.has_resource<Camera>()) {
        world.add_resource<Camera>(Camera{});
    }

    // Append built-in render systems (run after user systems)
    world.add_system<TileMapRenderSystem>();
    world.add_system<SpriteRenderSystem>();
```

**Step 2: Verify build and tests**

Run: `cmake --build build/debug && ctest --test-dir build/debug`
Expected: All tests pass, build succeeds.

**Step 3: Commit**

```bash
git add src/game.cpp
git commit -m "feat: auto-register built-in components, Camera, and render systems in run()

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

### Task 4: Rewrite Example to Use Built-in Rendering

**Files:**
- Modify: `examples/basic_tilemap/main.cpp`

Remove the custom RenderSystem entirely. Use `xebble::Position`, `xebble::Sprite`, and `xebble::TileMapLayer` instead of the game-local `Position`, `TileSprite`, and `Camera`. DungeonSystem creates TileMap entities and sets up entities with framework components.

**Step 1: Rewrite main.cpp**

Replace the entire contents of `examples/basic_tilemap/main.cpp` with:

```cpp
/// @file main.cpp
/// @brief Roguelike dungeon demo using xebble ECS with built-in rendering.
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

// --- Game-specific components ---
struct PlayerTag {};
struct MonsterInfo { std::string name; };
struct ItemInfo { std::string name; };

// --- Game-specific resources ---
struct GameState {
    Dungeon dungeon;
    int items_collected = 0;
    std::string message = "Welcome to the dungeon. Use WASD or arrow keys to move.";
    bool needs_rebuild = true;
};

// --- Systems ---

class DungeonSystem : public xebble::System {
    const xebble::SpriteSheet* sheet_ = nullptr;

public:
    void init(xebble::World& world) override {
        auto* assets = world.resource<xebble::AssetManager*>();
        sheet_ = &assets->get<xebble::SpriteSheet>("tiles");
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

        // Clear old entities
        std::vector<xebble::Entity> to_destroy;
        world.each<xebble::Position>([&](xebble::Entity e, xebble::Position&) {
            to_destroy.push_back(e);
        });
        // Also destroy tilemap entities
        world.each<xebble::TileMapLayer>([&](xebble::Entity e, xebble::TileMapLayer&) {
            to_destroy.push_back(e);
        });
        for (auto e : to_destroy) world.destroy(e);
        world.flush_destroyed();

        auto& dg = state.dungeon;

        // Create tilemap from dungeon data
        auto tilemap = std::make_shared<xebble::TileMap>(*sheet_, dg.width, dg.height, 2);
        for (int y = 0; y < dg.height; y++) {
            for (int x = 0; x < dg.width; x++) {
                tilemap->set_tile(0, x, y, dg.floor_tiles[y * dg.width + x]);
                uint32_t feat = dg.feature_tiles[y * dg.width + x];
                if (feat != UINT32_MAX)
                    tilemap->set_tile(1, x, y, feat);
            }
        }

        world.build_entity()
            .with<xebble::TileMapLayer>({tilemap, 0.0f})
            .build();

        // Create player
        world.build_entity()
            .with<PlayerTag>({})
            .with<xebble::Position>({static_cast<float>(dg.player_start_x * TILE_SIZE),
                                     static_cast<float>(dg.player_start_y * TILE_SIZE)})
            .with<xebble::Sprite>({sheet_, tiles::PLAYER, 3.0f})
            .build();

        // Create monsters
        for (auto& ent : dg.entities) {
            if (!ent.is_monster) continue;
            world.build_entity()
                .with<xebble::Position>({static_cast<float>(ent.x * TILE_SIZE),
                                         static_cast<float>(ent.y * TILE_SIZE)})
                .with<xebble::Sprite>({sheet_, ent.tile, 2.0f})
                .with<MonsterInfo>({ent.name})
                .build();
        }

        // Create items
        for (auto& ent : dg.entities) {
            if (ent.is_monster) continue;
            world.build_entity()
                .with<xebble::Position>({static_cast<float>(ent.x * TILE_SIZE),
                                         static_cast<float>(ent.y * TILE_SIZE)})
                .with<xebble::Sprite>({sheet_, ent.tile, 1.0f})
                .with<ItemInfo>({ent.name})
                .build();
        }

        // Update camera
        update_camera(world);
    }

    void update_camera(xebble::World& world) {
        auto& cam = world.resource<xebble::Camera>();
        world.each<PlayerTag, xebble::Position>([&](xebble::Entity, PlayerTag&, xebble::Position& pos) {
            cam.x = pos.x - static_cast<float>(VIRTUAL_W) / 2.0f + static_cast<float>(TILE_SIZE) / 2.0f;
            cam.y = pos.y - static_cast<float>(VIRTUAL_H - HUD_HEIGHT * TILE_SIZE) / 2.0f + static_cast<float>(TILE_SIZE) / 2.0f;
            auto& dg = world.resource<GameState>().dungeon;
            cam.x = std::clamp(cam.x, 0.0f, static_cast<float>(dg.width * TILE_SIZE - VIRTUAL_W));
            cam.y = std::clamp(cam.y, 0.0f, static_cast<float>(dg.height * TILE_SIZE - (VIRTUAL_H - HUD_HEIGHT * TILE_SIZE)));
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

        world.each<PlayerTag, xebble::Position>([&](xebble::Entity, PlayerTag&, xebble::Position& pos) {
            // Convert pixel position to tile coordinates for game logic
            int tile_x = static_cast<int>(pos.x) / TILE_SIZE;
            int tile_y = static_cast<int>(pos.y) / TILE_SIZE;
            int nx = tile_x + dx;
            int ny = tile_y + dy;

            if (!state.dungeon.is_walkable(nx, ny)) {
                state.message = "You bump into a wall.";
                return;
            }

            // Check for monster
            bool blocked = false;
            world.each<MonsterInfo, xebble::Position>([&](xebble::Entity, MonsterInfo& info, xebble::Position& mpos) {
                int mx = static_cast<int>(mpos.x) / TILE_SIZE;
                int my = static_cast<int>(mpos.y) / TILE_SIZE;
                if (mx == nx && my == ny) {
                    state.message = std::format("You see a {}.", info.name);
                    blocked = true;
                }
            });
            if (blocked) return;

            pos.x = static_cast<float>(nx * TILE_SIZE);
            pos.y = static_cast<float>(ny * TILE_SIZE);

            // Check for item pickup
            bool picked_up = false;
            world.each<ItemInfo, xebble::Position>([&](xebble::Entity ie, ItemInfo& info, xebble::Position& ipos) {
                int ix = static_cast<int>(ipos.x) / TILE_SIZE;
                int iy = static_cast<int>(ipos.y) / TILE_SIZE;
                if (ix == nx && iy == ny) {
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
            auto& cam = world.resource<xebble::Camera>();
            cam.x = pos.x - static_cast<float>(VIRTUAL_W) / 2.0f + static_cast<float>(TILE_SIZE) / 2.0f;
            cam.y = pos.y - static_cast<float>(VIRTUAL_H - HUD_HEIGHT * TILE_SIZE) / 2.0f + static_cast<float>(TILE_SIZE) / 2.0f;
            cam.x = std::clamp(cam.x, 0.0f, static_cast<float>(state.dungeon.width * TILE_SIZE - VIRTUAL_W));
            cam.y = std::clamp(cam.y, 0.0f, static_cast<float>(state.dungeon.height * TILE_SIZE - static_cast<int>(VIRTUAL_H - HUD_HEIGHT * TILE_SIZE)));
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
            renderer.submit_instances(hud_bg, tile_sheet_->texture(), 10.0f);
        }

        // HUD text
        int player_x = 0, player_y = 0;
        world.each<PlayerTag, xebble::Position>([&](xebble::Entity, PlayerTag&, xebble::Position& pos) {
            player_x = static_cast<int>(pos.x) / TILE_SIZE;
            player_y = static_cast<int>(pos.y) / TILE_SIZE;
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
                renderer.submit_instances(glyphs, font_->sheet().texture(), 11.0f);
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

    // Only game-specific components needed
    world.register_component<PlayerTag>();
    world.register_component<MonsterInfo>();
    world.register_component<ItemInfo>();

    world.add_resource<GameState>({});

    // Only game logic systems — no render system needed!
    world.add_system<DungeonSystem>();
    world.add_system<InputSystem>();
    world.add_system<HudSystem>();

    return xebble::run(std::move(world), {
        .window = {.title = "Xebble - Roguelike Demo", .width = 1280, .height = 720},
        .renderer = {.virtual_width = VIRTUAL_W, .virtual_height = VIRTUAL_H},
        .assets = {.directory = assets_dir, .manifest = manifest_path},
    });
}
```

Key changes from previous version:
- **No RenderSystem** — framework handles it
- **No game-local Position/TileSprite/Camera** — uses `xebble::Position`, `xebble::Sprite`, `xebble::Camera`
- **Position is in pixels** (not tiles) — multiply by TILE_SIZE when creating entities, divide when doing tile-coordinate game logic
- **DungeonSystem creates TileMap entities** with `xebble::TileMapLayer` using `shared_ptr<TileMap>`
- **HUD uses z_order 10.0f/11.0f** to draw on top of everything (sprites use 1-3, tilemap uses 0)
- **Camera resource no longer manually registered** — `run()` does it

**Step 2: Build and test**

Run: `cmake --build build/debug --target basic_tilemap 2>&1`
Expected: Compiles.

Run the app manually to verify: dungeon renders, WASD works, items collectible, R regenerates, HUD displays.

**Step 3: Run full test suite**

Run: `ctest --test-dir build/debug`
Expected: All tests pass.

**Step 4: Commit**

```bash
git add examples/basic_tilemap/main.cpp
git commit -m "refactor: rewrite example to use built-in rendering — no custom RenderSystem

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```
