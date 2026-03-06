# Enhanced Roguelike Example Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the basic colored-squares example with a procedurally generated roguelike dungeon demo using the Adam Bolt Angband 16x16 tileset, complete with player movement, entity interaction, scrolling camera, and a HUD.

**Architecture:** The example is a single `main.cpp` plus a few header files for dungeon generation and tile constants. All game state lives in the `Game` subclass. The tileset is downloaded from the Angband GitHub repo. A bitmap font is generated at startup for HUD text. The dungeon is procedurally generated using random room placement with L-shaped corridor connections.

**Tech Stack:** Xebble API (Window, Renderer, SpriteSheet, TileMap, Sprite, BitmapFont, AssetManager, Game), stb_image_write for font generation. The tileset source is the Adam Bolt 16x16.png from `github.com/angband/angband`.

**Important context:**
- The tileset is 1024px wide = 64 columns of 16x16 tiles. Rows vary (typically 88+ rows).
- Tile positions from the Angband `.prf` files use hex `0x80` as base offset. Row = `attr - 0x80`, Col = `char - 0x80`. Linear index = `row * 64 + col`.
- The example currently uses `stb_image_write` to generate a test tileset at startup. We'll replace that with the real Angband tileset.
- The example's `draw()` method manually builds `SpriteInstance` vectors and calls `renderer.submit_instances()`.
- The asset system loads spritesheets via `manifest.toml`.
- `SpriteSheet::region(uint32_t index)` returns UV coordinates for a linear tile index.

---

## Task 1: Add the Angband Tileset and Attribution

**Files:**
- Create: `examples/basic_tilemap/assets/angband-16x16.png` (downloaded)
- Create: `examples/basic_tilemap/assets/TILESET-LICENSE.txt`
- Modify: `examples/basic_tilemap/assets/manifest.toml`

**Step 1: Download the Adam Bolt 16x16 tileset from the Angband GitHub repo**

Run:
```bash
curl -L -o examples/basic_tilemap/assets/angband-16x16.png \
  "https://raw.githubusercontent.com/angband/angband/master/lib/tiles/adam-bolt/16x16.png"
```

Verify it downloaded:
```bash
file examples/basic_tilemap/assets/angband-16x16.png
```

Expected: PNG image data, should be 1024px wide.

**Step 2: Verify the tileset dimensions**

Run:
```bash
python3 -c "
from PIL import Image
img = Image.open('examples/basic_tilemap/assets/angband-16x16.png')
print(f'{img.width}x{img.height}, cols={img.width//16}, rows={img.height//16}')
" 2>/dev/null || identify examples/basic_tilemap/assets/angband-16x16.png 2>/dev/null || file examples/basic_tilemap/assets/angband-16x16.png
```

Record the number of columns — we need this for tile index calculations. Expected: 64 columns.

**Step 3: Create attribution file**

Create `examples/basic_tilemap/assets/TILESET-LICENSE.txt`:
```
Angband Adam Bolt 16x16 Tileset
================================

This tileset is from the Angband roguelike game project.
Source: https://github.com/angband/angband/tree/master/lib/tiles/adam-bolt

The Angband game and its assets are distributed under the Angband License,
a permissive open-source license similar to the BSD license.

See https://rephial.org/wiki/License for full license terms.
```

**Step 4: Update manifest.toml**

Replace the contents of `examples/basic_tilemap/assets/manifest.toml`:
```toml
[spritesheets.tiles]
path = "angband-16x16.png"
tile_width = 16
tile_height = 16
```

**Step 5: Remove old generated tiles.png from .gitignore if present, and delete old tiles.png**

Check `examples/basic_tilemap/assets/.gitignore` and remove any entry for `tiles.png`. Delete `tiles.png` if it exists.

**Step 6: Commit**

```bash
git add examples/basic_tilemap/assets/
git commit -m "feat(example): add Angband Adam Bolt 16x16 tileset with attribution"
```

---

## Task 2: Define Tile Constants

**Files:**
- Create: `examples/basic_tilemap/tiles.hpp`

**Step 1: Create tile constants header**

Create `examples/basic_tilemap/tiles.hpp` with named constants derived from the Angband `.prf` tile mappings. The tileset has 64 columns. Linear index = `row * 64 + col`, where row = `attr - 0x80` and col = `char - 0x80`.

```cpp
#pragma once

#include <cstdint>

/// Tile indices into the Adam Bolt 16x16 Angband tileset (64 columns wide).
/// Derived from angband/lib/tiles/adam-bolt/graf-new.prf and flvr-new.prf.
/// Format: row = (attr - 0x80), col = (char - 0x80), index = row * 64 + col.
namespace tiles {

constexpr uint32_t COLS = 64;

constexpr uint32_t idx(uint8_t attr, uint8_t chr) {
    return static_cast<uint32_t>(attr - 0x80) * COLS + (chr - 0x80);
}

// --- Terrain ---
constexpr uint32_t FLOOR           = idx(0x80, 0x83);  // Open floor (torchlight)
constexpr uint32_t GRANITE_WALL    = idx(0x80, 0x86);  // Granite wall
constexpr uint32_t PERMANENT_WALL  = idx(0x80, 0x95);  // Permanent/border wall
constexpr uint32_t OPEN_DOOR       = idx(0x82, 0x84);  // Open door
constexpr uint32_t CLOSED_DOOR     = idx(0x82, 0x83);  // Closed door
constexpr uint32_t STAIRS_UP       = idx(0x80, 0x98);  // Staircase up
constexpr uint32_t STAIRS_DOWN     = idx(0x80, 0x9B);  // Staircase down
constexpr uint32_t RUBBLE          = idx(0x80, 0x9E);  // Passable rubble

// --- Player ---
constexpr uint32_t PLAYER          = idx(0x92, 0x80);  // Player character (@)

// --- Monsters ---
constexpr uint32_t SMALL_KOBOLD    = idx(0xA8, 0x99);
constexpr uint32_t KOBOLD          = idx(0xA8, 0x9A);
constexpr uint32_t LARGE_KOBOLD    = idx(0xA8, 0x9B);
constexpr uint32_t CAVE_ORC        = idx(0xA9, 0x8F);
constexpr uint32_t SNAGA           = idx(0xA9, 0x8E);
constexpr uint32_t LARGE_SNAKE     = idx(0xA2, 0x85);  // Large white snake
constexpr uint32_t FRUIT_BAT       = idx(0xA5, 0x8F);
constexpr uint32_t GIANT_RAT       = idx(0xAC, 0x87);  // Giant grey rat
constexpr uint32_t WOLF            = idx(0x9D, 0x9E);
constexpr uint32_t FOREST_TROLL    = idx(0xA3, 0x89);
constexpr uint32_t WHITE_JELLY     = idx(0xA8, 0x8A);
constexpr uint32_t FLOATING_EYE    = idx(0xA6, 0x9B);

// --- Items ---
constexpr uint32_t GOLD            = idx(0x83, 0x93);  // Gold pile
constexpr uint32_t SCROLL          = idx(0x83, 0x9D);  // Scroll
constexpr uint32_t POTION_RED      = idx(0x85, 0x83);  // Red potion
constexpr uint32_t POTION_BLUE     = idx(0x85, 0x85);  // Blue potion
constexpr uint32_t POTION_GREEN    = idx(0x85, 0x84);  // Icky green potion
constexpr uint32_t FOOD            = idx(0x8E, 0x84);  // Ration of food
constexpr uint32_t TORCH           = idx(0x8E, 0x8B);  // Wooden torch
constexpr uint32_t DAGGER          = idx(0x8A, 0x8F);  // Dagger

// --- Monster roster for random placement ---
constexpr uint32_t MONSTERS[] = {
    SMALL_KOBOLD, KOBOLD, LARGE_KOBOLD, CAVE_ORC, SNAGA,
    LARGE_SNAKE, FRUIT_BAT, GIANT_RAT, WOLF, FOREST_TROLL,
    WHITE_JELLY, FLOATING_EYE,
};
constexpr const char* MONSTER_NAMES[] = {
    "Small Kobold", "Kobold", "Large Kobold", "Cave Orc", "Snaga",
    "Large White Snake", "Fruit Bat", "Giant Grey Rat", "Wolf", "Forest Troll",
    "White Jelly", "Floating Eye",
};
constexpr int NUM_MONSTERS = sizeof(MONSTERS) / sizeof(MONSTERS[0]);

// --- Item roster for random placement ---
constexpr uint32_t ITEMS[] = {
    GOLD, GOLD, GOLD, SCROLL, POTION_RED, POTION_BLUE,
    POTION_GREEN, FOOD, TORCH, DAGGER,
};
constexpr const char* ITEM_NAMES[] = {
    "Gold", "Gold", "Gold", "Scroll", "Red Potion", "Blue Potion",
    "Green Potion", "Ration of Food", "Wooden Torch", "Dagger",
};
constexpr int NUM_ITEMS = sizeof(ITEMS) / sizeof(ITEMS[0]);

} // namespace tiles
```

**Step 2: Build to verify the header compiles**

Temporarily `#include "tiles.hpp"` in `main.cpp` and build:
```bash
cmake --build build/debug 2>&1 | tail -5
```

Expected: Compiles cleanly.

**Step 3: Commit**

```bash
git add examples/basic_tilemap/tiles.hpp
git commit -m "feat(example): add named tile constants for Angband tileset"
```

---

## Task 3: Dungeon Generator

**Files:**
- Create: `examples/basic_tilemap/dungeon.hpp`

**Step 1: Create the dungeon generator**

This is a self-contained header-only dungeon generator. It produces a 2D grid of tile indices plus entity placements.

```cpp
#pragma once

#include "tiles.hpp"
#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

struct Room {
    int x, y, w, h;  // Position and size in tiles

    int center_x() const { return x + w / 2; }
    int center_y() const { return y + h / 2; }

    bool intersects(const Room& other, int padding = 1) const {
        return !(x - padding >= other.x + other.w ||
                 x + w + padding <= other.x ||
                 y - padding >= other.y + other.h ||
                 y + h + padding <= other.y);
    }
};

struct Entity {
    int x, y;
    uint32_t tile;
    std::string name;
    bool is_monster;   // true = monster (blocks movement), false = item (collectible)
    bool alive = true; // false = collected/removed
};

struct Dungeon {
    int width = 80;
    int height = 50;
    std::vector<uint32_t> floor_tiles;  // Layer 0: floor/walls
    std::vector<uint32_t> feature_tiles; // Layer 1: doors, stairs, decorations
    std::vector<Entity> entities;
    std::vector<Room> rooms;
    int player_start_x = 0;
    int player_start_y = 0;

    uint32_t floor_at(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return tiles::PERMANENT_WALL;
        return floor_tiles[y * width + x];
    }

    bool is_walkable(int x, int y) const {
        uint32_t t = floor_at(x, y);
        return t == tiles::FLOOR || t == tiles::OPEN_DOOR ||
               t == tiles::STAIRS_UP || t == tiles::STAIRS_DOWN ||
               t == tiles::RUBBLE;
    }

    // Check if a monster blocks this position
    Entity* monster_at(int x, int y) {
        for (auto& e : entities) {
            if (e.alive && e.is_monster && e.x == x && e.y == y)
                return &e;
        }
        return nullptr;
    }

    // Check if an item is at this position
    Entity* item_at(int x, int y) {
        for (auto& e : entities) {
            if (e.alive && !e.is_monster && e.x == x && e.y == y)
                return &e;
        }
        return nullptr;
    }
};

inline Dungeon generate_dungeon(uint32_t seed = 0) {
    Dungeon dg;
    dg.floor_tiles.resize(dg.width * dg.height, tiles::GRANITE_WALL);
    dg.feature_tiles.resize(dg.width * dg.height, UINT32_MAX); // UINT32_MAX = empty

    std::mt19937 rng(seed ? seed : std::random_device{}());

    // --- Place rooms ---
    constexpr int MAX_ROOMS = 12;
    constexpr int MIN_ROOM_SIZE = 4;
    constexpr int MAX_ROOM_SIZE = 10;
    constexpr int MAX_ATTEMPTS = 200;

    int attempts = 0;
    while (dg.rooms.size() < MAX_ROOMS && attempts < MAX_ATTEMPTS) {
        attempts++;
        int w = std::uniform_int_distribution<int>(MIN_ROOM_SIZE, MAX_ROOM_SIZE)(rng);
        int h = std::uniform_int_distribution<int>(MIN_ROOM_SIZE, MAX_ROOM_SIZE)(rng);
        int x = std::uniform_int_distribution<int>(1, dg.width - w - 1)(rng);
        int y = std::uniform_int_distribution<int>(1, dg.height - h - 1)(rng);

        Room room{x, y, w, h};

        bool overlap = false;
        for (auto& existing : dg.rooms) {
            if (room.intersects(existing, 2)) {
                overlap = true;
                break;
            }
        }
        if (overlap) continue;

        // Carve room
        for (int ry = room.y; ry < room.y + room.h; ry++)
            for (int rx = room.x; rx < room.x + room.w; rx++)
                dg.floor_tiles[ry * dg.width + rx] = tiles::FLOOR;

        dg.rooms.push_back(room);
    }

    // --- Connect rooms with L-shaped corridors ---
    for (size_t i = 1; i < dg.rooms.size(); i++) {
        int cx1 = dg.rooms[i - 1].center_x();
        int cy1 = dg.rooms[i - 1].center_y();
        int cx2 = dg.rooms[i].center_x();
        int cy2 = dg.rooms[i].center_y();

        // Randomly choose horizontal-first or vertical-first
        bool horiz_first = std::uniform_int_distribution<int>(0, 1)(rng);

        auto carve_h = [&](int x1, int x2, int y) {
            int start = std::min(x1, x2);
            int end = std::max(x1, x2);
            for (int x = start; x <= end; x++)
                if (dg.floor_tiles[y * dg.width + x] == tiles::GRANITE_WALL)
                    dg.floor_tiles[y * dg.width + x] = tiles::FLOOR;
        };

        auto carve_v = [&](int y1, int y2, int x) {
            int start = std::min(y1, y2);
            int end = std::max(y1, y2);
            for (int y = start; y <= end; y++)
                if (dg.floor_tiles[y * dg.width + x] == tiles::GRANITE_WALL)
                    dg.floor_tiles[y * dg.width + x] = tiles::FLOOR;
        };

        if (horiz_first) {
            carve_h(cx1, cx2, cy1);
            carve_v(cy1, cy2, cx2);
        } else {
            carve_v(cy1, cy2, cx1);
            carve_h(cx1, cx2, cy2);
        }
    }

    // --- Place doors where corridors meet room edges ---
    // Simple heuristic: look for floor tiles with exactly 2 wall neighbors on
    // opposite sides (N/S or E/W) and floor neighbors on the other axis.
    // This catches narrow corridor-room transitions.
    for (int y = 1; y < dg.height - 1; y++) {
        for (int x = 1; x < dg.width - 1; x++) {
            if (dg.floor_tiles[y * dg.width + x] != tiles::FLOOR) continue;

            uint32_t n = dg.floor_tiles[(y-1) * dg.width + x];
            uint32_t s = dg.floor_tiles[(y+1) * dg.width + x];
            uint32_t e = dg.floor_tiles[y * dg.width + (x+1)];
            uint32_t w = dg.floor_tiles[y * dg.width + (x-1)];

            bool ns_walls = (n == tiles::GRANITE_WALL && s == tiles::GRANITE_WALL);
            bool ew_floors = (e == tiles::FLOOR && w == tiles::FLOOR);
            bool ew_walls = (e == tiles::GRANITE_WALL && w == tiles::GRANITE_WALL);
            bool ns_floors = (n == tiles::FLOOR && s == tiles::FLOOR);

            if ((ns_walls && ew_floors) || (ew_walls && ns_floors)) {
                // 30% chance of a door
                if (std::uniform_int_distribution<int>(0, 9)(rng) < 3) {
                    dg.feature_tiles[y * dg.width + x] = tiles::OPEN_DOOR;
                }
            }
        }
    }

    // --- Place stairs in first and last rooms ---
    if (dg.rooms.size() >= 2) {
        auto& first = dg.rooms.front();
        auto& last = dg.rooms.back();
        dg.feature_tiles[first.center_y() * dg.width + first.center_x()] = tiles::STAIRS_UP;
        dg.feature_tiles[last.center_y() * dg.width + last.center_x()] = tiles::STAIRS_DOWN;
    }

    // --- Place player in first room ---
    if (!dg.rooms.empty()) {
        auto& start_room = dg.rooms.front();
        dg.player_start_x = start_room.center_x();
        dg.player_start_y = start_room.center_y();
        // Offset from stairs if they overlap
        if (dg.feature_tiles[dg.player_start_y * dg.width + dg.player_start_x] != UINT32_MAX) {
            dg.player_start_x = start_room.x + 1;
            dg.player_start_y = start_room.y + 1;
        }
    }

    // --- Place monsters (1-2 per room, skip first room) ---
    for (size_t i = 1; i < dg.rooms.size(); i++) {
        auto& room = dg.rooms[i];
        int num = std::uniform_int_distribution<int>(1, 2)(rng);
        for (int j = 0; j < num; j++) {
            int ex = std::uniform_int_distribution<int>(room.x + 1, room.x + room.w - 2)(rng);
            int ey = std::uniform_int_distribution<int>(room.y + 1, room.y + room.h - 2)(rng);
            int mi = std::uniform_int_distribution<int>(0, tiles::NUM_MONSTERS - 1)(rng);
            dg.entities.push_back({ex, ey, tiles::MONSTERS[mi], tiles::MONSTER_NAMES[mi], true});
        }
    }

    // --- Place items (1-2 per room, including first room) ---
    for (auto& room : dg.rooms) {
        int num = std::uniform_int_distribution<int>(1, 2)(rng);
        for (int j = 0; j < num; j++) {
            int ex = std::uniform_int_distribution<int>(room.x + 1, room.x + room.w - 2)(rng);
            int ey = std::uniform_int_distribution<int>(room.y + 1, room.y + room.h - 2)(rng);
            // Don't place on player start
            if (ex == dg.player_start_x && ey == dg.player_start_y) continue;
            int ii = std::uniform_int_distribution<int>(0, tiles::NUM_ITEMS - 1)(rng);
            dg.entities.push_back({ex, ey, tiles::ITEMS[ii], tiles::ITEM_NAMES[ii], false});
        }
    }

    // --- Scatter some rubble in corridors ---
    for (int y = 1; y < dg.height - 1; y++) {
        for (int x = 1; x < dg.width - 1; x++) {
            if (dg.floor_tiles[y * dg.width + x] != tiles::FLOOR) continue;
            if (dg.feature_tiles[y * dg.width + x] != UINT32_MAX) continue;
            if (std::uniform_int_distribution<int>(0, 99)(rng) < 2) {
                dg.feature_tiles[y * dg.width + x] = tiles::RUBBLE;
            }
        }
    }

    return dg;
}
```

**Step 2: Build to verify**

Add a temporary include in main.cpp and build:
```bash
cmake --build build/debug 2>&1 | tail -5
```

Expected: Compiles cleanly.

**Step 3: Commit**

```bash
git add examples/basic_tilemap/dungeon.hpp
git commit -m "feat(example): add procedural dungeon generator with rooms and corridors"
```

---

## Task 4: Bitmap Font Generation

**Files:**
- Create: `examples/basic_tilemap/font_gen.hpp`

**Step 1: Create a minimal CP437 bitmap font generator**

This generates an 8x8 pixel font atlas at runtime using hardcoded pixel data for printable ASCII (space through tilde, 95 characters). The atlas is written as a PNG for loading via the asset system.

The font data is based on the classic IBM PC CP437 8x8 font, which is in the public domain.

```cpp
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

#include <stb_image_write.h>

namespace font_gen {

// CP437 8x8 font data for printable ASCII (32-126).
// Each character is 8 bytes, one per row, MSB = leftmost pixel.
// This is the classic IBM PC BIOS font (public domain).
// clang-format off
constexpr uint8_t FONT_DATA[] = {
    // 32: space
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 33: !
    0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00,
    // 34: "
    0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00,
    // 35: #
    0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00,
    // 36: $
    0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00,
    // 37: %
    0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00,
    // 38: &
    0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00,
    // 39: '
    0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00,
    // 40: (
    0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00,
    // 41: )
    0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00,
    // 42: *
    0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,
    // 43: +
    0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,
    // 44: ,
    0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30,
    // 45: -
    0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,
    // 46: .
    0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,
    // 47: /
    0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,
    // 48: 0
    0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0x00,
    // 49: 1
    0x18,0x38,0x78,0x18,0x18,0x18,0x7E,0x00,
    // 50: 2
    0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00,
    // 51: 3
    0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00,
    // 52: 4
    0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00,
    // 53: 5
    0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00,
    // 54: 6
    0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00,
    // 55: 7
    0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00,
    // 56: 8
    0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00,
    // 57: 9
    0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00,
    // 58: :
    0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00,
    // 59: ;
    0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30,
    // 60: <
    0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00,
    // 61: =
    0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00,
    // 62: >
    0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00,
    // 63: ?
    0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00,
    // 64: @
    0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00,
    // 65: A
    0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00,
    // 66: B
    0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00,
    // 67: C
    0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00,
    // 68: D
    0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00,
    // 69: E
    0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00,
    // 70: F
    0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00,
    // 71: G
    0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00,
    // 72: H
    0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00,
    // 73: I
    0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,
    // 74: J
    0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00,
    // 75: K
    0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00,
    // 76: L
    0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,
    // 77: M
    0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00,
    // 78: N
    0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00,
    // 79: O
    0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,
    // 80: P
    0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00,
    // 81: Q
    0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06,
    // 82: R
    0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00,
    // 83: S
    0x7C,0xC6,0xC0,0x7C,0x06,0xC6,0x7C,0x00,
    // 84: T
    0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00,
    // 85: U
    0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,
    // 86: V
    0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,
    // 87: W
    0xC6,0xC6,0xD6,0xFE,0xFE,0xEE,0xC6,0x00,
    // 88: X
    0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00,
    // 89: Y
    0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00,
    // 90: Z
    0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00,
    // 91: [
    0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,
    // 92: backslash
    0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00,
    // 93: ]
    0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,
    // 94: ^
    0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,
    // 95: _
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,
    // 96: `
    0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,
    // 97: a
    0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00,
    // 98: b
    0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00,
    // 99: c
    0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00,
    // 100: d
    0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,
    // 101: e
    0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00,
    // 102: f
    0x1C,0x36,0x30,0x78,0x30,0x30,0x78,0x00,
    // 103: g
    0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8,
    // 104: h
    0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00,
    // 105: i
    0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00,
    // 106: j
    0x06,0x00,0x06,0x06,0x06,0x66,0x66,0x3C,
    // 107: k
    0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00,
    // 108: l
    0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,
    // 109: m
    0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0x00,
    // 110: n
    0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00,
    // 111: o
    0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00,
    // 112: p
    0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0,
    // 113: q
    0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E,
    // 114: r
    0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00,
    // 115: s
    0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00,
    // 116: t
    0x30,0x30,0x7C,0x30,0x30,0x36,0x1C,0x00,
    // 117: u
    0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00,
    // 118: v
    0x00,0x00,0xC6,0xC6,0x6C,0x38,0x10,0x00,
    // 119: w
    0x00,0x00,0xC6,0xD6,0xD6,0xFE,0x6C,0x00,
    // 120: x
    0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00,
    // 121: y
    0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC,
    // 122: z
    0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00,
    // 123: {
    0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00,
    // 124: |
    0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,
    // 125: }
    0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00,
    // 126: ~
    0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,
};
// clang-format on

constexpr int GLYPH_W = 8;
constexpr int GLYPH_H = 8;
constexpr int CHARS_PER_ROW = 16;
constexpr int FIRST_CHAR = 32;
constexpr int LAST_CHAR = 126;
constexpr int NUM_CHARS = LAST_CHAR - FIRST_CHAR + 1; // 95
constexpr int ROWS = (NUM_CHARS + CHARS_PER_ROW - 1) / CHARS_PER_ROW; // 6
constexpr int ATLAS_W = CHARS_PER_ROW * GLYPH_W;  // 128
constexpr int ATLAS_H = ROWS * GLYPH_H;           // 48

/// Generate a bitmap font atlas PNG at the given path.
/// White glyphs on transparent background.
inline bool generate_font(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) return true;

    std::vector<uint8_t> pixels(ATLAS_W * ATLAS_H * 4, 0);

    for (int ci = 0; ci < NUM_CHARS; ci++) {
        int col = ci % CHARS_PER_ROW;
        int row = ci / CHARS_PER_ROW;
        const uint8_t* glyph = &FONT_DATA[ci * GLYPH_H];

        for (int gy = 0; gy < GLYPH_H; gy++) {
            uint8_t bits = glyph[gy];
            for (int gx = 0; gx < GLYPH_W; gx++) {
                if (bits & (0x80 >> gx)) {
                    int px = col * GLYPH_W + gx;
                    int py = row * GLYPH_H + gy;
                    int idx = (py * ATLAS_W + px) * 4;
                    pixels[idx + 0] = 255; // R
                    pixels[idx + 1] = 255; // G
                    pixels[idx + 2] = 255; // B
                    pixels[idx + 3] = 255; // A
                }
            }
        }
    }

    return stbi_write_png(path.string().c_str(), ATLAS_W, ATLAS_H, 4, pixels.data(), ATLAS_W * 4) != 0;
}

/// The charset string for BitmapFont, matching the atlas layout.
/// Space (32) through tilde (126) in order.
inline std::string font_charset() {
    std::string s;
    for (int c = FIRST_CHAR; c <= LAST_CHAR; c++)
        s += static_cast<char>(c);
    return s;
}

} // namespace font_gen
```

**Step 2: Build to verify**

```bash
cmake --build build/debug 2>&1 | tail -5
```

**Step 3: Commit**

```bash
git add examples/basic_tilemap/font_gen.hpp
git commit -m "feat(example): add CP437 bitmap font atlas generator"
```

---

## Task 5: Rewrite main.cpp — The Roguelike Demo

**Files:**
- Modify: `examples/basic_tilemap/main.cpp`

**Step 1: Rewrite `main.cpp` with the full roguelike demo**

Replace the entire contents of `examples/basic_tilemap/main.cpp`:

```cpp
/// @file main.cpp
/// @brief Roguelike dungeon demo using the Angband Adam Bolt 16x16 tileset.
///
/// Generates a procedural dungeon with rooms and corridors. Move with WASD or
/// arrow keys. Walk over items to collect them. Bump into monsters to see their
/// name. Press R to regenerate the dungeon.

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
#include <memory>
#include <optional>
#include <string>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

/// Find the assets directory — checks .app bundle Resources first, then relative path.
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
constexpr uint32_t VIEW_TILES_X = VIRTUAL_W / TILE_SIZE;   // 40
constexpr uint32_t VIEW_TILES_Y = VIRTUAL_H / TILE_SIZE;   // 22
constexpr uint32_t HUD_HEIGHT = 2; // Reserve 2 tile-rows at bottom for HUD
constexpr uint32_t MAP_VIEW_TILES_Y = VIEW_TILES_Y - HUD_HEIGHT; // 20

} // namespace

class RoguelikeDemo : public xebble::Game {
    const xebble::SpriteSheet* sheet_ = nullptr;
    const xebble::BitmapFont* font_ = nullptr;
    std::optional<xebble::TileMap> tilemap_;
    Dungeon dungeon_;

    int player_x_ = 0;
    int player_y_ = 0;
    int camera_x_ = 0; // Top-left tile visible
    int camera_y_ = 0;
    int items_collected_ = 0;
    std::string message_ = "Welcome to the dungeon. Use WASD or arrow keys to move.";
    uint32_t dungeon_seed_ = 0;

public:
    void init(xebble::Renderer& renderer, xebble::AssetManager& assets) override {
        sheet_ = &assets.get<xebble::SpriteSheet>("tiles");
        font_ = &assets.get<xebble::BitmapFont>("font");
        generate_new_dungeon();
    }

    void generate_new_dungeon() {
        dungeon_ = generate_dungeon(dungeon_seed_);
        player_x_ = dungeon_.player_start_x;
        player_y_ = dungeon_.player_start_y;
        items_collected_ = 0;
        message_ = "Welcome to the dungeon. Use WASD or arrow keys to move.";

        // Build tilemap from dungeon data
        tilemap_.emplace(*sheet_, dungeon_.width, dungeon_.height, 2);
        for (int y = 0; y < dungeon_.height; y++) {
            for (int x = 0; x < dungeon_.width; x++) {
                // Layer 0: floor and walls
                tilemap_->set_tile(0, x, y, dungeon_.floor_tiles[y * dungeon_.width + x]);
                // Layer 1: features (doors, stairs, rubble)
                uint32_t feat = dungeon_.feature_tiles[y * dungeon_.width + x];
                if (feat != UINT32_MAX) {
                    tilemap_->set_tile(1, x, y, feat);
                }
            }
        }

        update_camera();
    }

    void try_move(int dx, int dy) {
        int nx = player_x_ + dx;
        int ny = player_y_ + dy;

        if (!dungeon_.is_walkable(nx, ny)) {
            message_ = "You bump into a wall.";
            return;
        }

        // Check for monster
        if (auto* monster = dungeon_.monster_at(nx, ny)) {
            message_ = std::format("You see a {}.", monster->name);
            return; // Blocked by monster
        }

        // Move player
        player_x_ = nx;
        player_y_ = ny;

        // Check for item pickup
        if (auto* item = dungeon_.item_at(nx, ny)) {
            item->alive = false;
            items_collected_++;
            message_ = std::format("You picked up {}.", item->name);
        } else {
            // Check what we're standing on
            int idx = ny * dungeon_.width + nx;
            uint32_t feat = dungeon_.feature_tiles[idx];
            if (feat == tiles::STAIRS_DOWN) {
                message_ = "You see a staircase leading down.";
            } else if (feat == tiles::STAIRS_UP) {
                message_ = "You see a staircase leading up.";
            } else if (feat == tiles::OPEN_DOOR) {
                message_ = "You pass through a doorway.";
            } else {
                message_ = "";
            }
        }

        update_camera();
    }

    void update_camera() {
        // Center camera on player, clamped to map bounds
        camera_x_ = player_x_ - static_cast<int>(VIEW_TILES_X) / 2;
        camera_y_ = player_y_ - static_cast<int>(MAP_VIEW_TILES_Y) / 2;
        camera_x_ = std::clamp(camera_x_, 0, std::max(0, dungeon_.width - static_cast<int>(VIEW_TILES_X)));
        camera_y_ = std::clamp(camera_y_, 0, std::max(0, dungeon_.height - static_cast<int>(MAP_VIEW_TILES_Y)));
    }

    void update(float /*dt*/) override {}

    void on_event(const xebble::Event& event) override {
        if (event.type == xebble::EventType::KeyPress) {
            switch (event.key().key) {
                case xebble::Key::W: case xebble::Key::Up:    try_move(0, -1); break;
                case xebble::Key::S: case xebble::Key::Down:  try_move(0,  1); break;
                case xebble::Key::A: case xebble::Key::Left:  try_move(-1, 0); break;
                case xebble::Key::D: case xebble::Key::Right: try_move( 1, 0); break;
                case xebble::Key::R:
                    dungeon_seed_ = 0; // Random new seed
                    generate_new_dungeon();
                    break;
                case xebble::Key::Escape:
                    std::exit(0);
                    break;
                default: break;
            }
        }
    }

    void draw(xebble::Renderer& renderer) override {
        float cam_offset_x = static_cast<float>(camera_x_ * TILE_SIZE);
        float cam_offset_y = static_cast<float>(camera_y_ * TILE_SIZE);
        float map_area_h = static_cast<float>(MAP_VIEW_TILES_Y * TILE_SIZE);

        // --- Draw tilemap layers (floor, features) ---
        for (uint32_t layer = 0; layer < tilemap_->layer_count(); layer++) {
            std::vector<xebble::SpriteInstance> instances;

            for (int vy = 0; vy < static_cast<int>(MAP_VIEW_TILES_Y) + 1; vy++) {
                for (int vx = 0; vx < static_cast<int>(VIEW_TILES_X) + 1; vx++) {
                    int tx = camera_x_ + vx;
                    int ty = camera_y_ + vy;
                    if (tx < 0 || tx >= dungeon_.width || ty < 0 || ty >= dungeon_.height)
                        continue;

                    auto tile = tilemap_->tile_at(layer, tx, ty);
                    if (!tile) continue;

                    auto uv = sheet_->region(*tile);
                    instances.push_back({
                        .pos_x = static_cast<float>(vx * TILE_SIZE),
                        .pos_y = static_cast<float>(vy * TILE_SIZE),
                        .uv_x = uv.x, .uv_y = uv.y,
                        .uv_w = uv.w, .uv_h = uv.h,
                        .quad_w = static_cast<float>(TILE_SIZE),
                        .quad_h = static_cast<float>(TILE_SIZE),
                        .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                    });
                }
            }
            if (!instances.empty()) {
                renderer.submit_instances(instances, sheet_->texture(),
                    static_cast<float>(layer));
            }
        }

        // --- Draw entities (monsters and items) ---
        {
            std::vector<xebble::SpriteInstance> instances;
            for (auto& e : dungeon_.entities) {
                if (!e.alive) continue;
                // Only draw if visible
                int sx = e.x - camera_x_;
                int sy = e.y - camera_y_;
                if (sx < 0 || sx >= static_cast<int>(VIEW_TILES_X) ||
                    sy < 0 || sy >= static_cast<int>(MAP_VIEW_TILES_Y))
                    continue;

                auto uv = sheet_->region(e.tile);
                instances.push_back({
                    .pos_x = static_cast<float>(sx * TILE_SIZE),
                    .pos_y = static_cast<float>(sy * TILE_SIZE),
                    .uv_x = uv.x, .uv_y = uv.y,
                    .uv_w = uv.w, .uv_h = uv.h,
                    .quad_w = static_cast<float>(TILE_SIZE),
                    .quad_h = static_cast<float>(TILE_SIZE),
                    .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                });
            }
            if (!instances.empty()) {
                renderer.submit_instances(instances, sheet_->texture(), 2.0f);
            }
        }

        // --- Draw player ---
        {
            int px = player_x_ - camera_x_;
            int py = player_y_ - camera_y_;
            auto uv = sheet_->region(tiles::PLAYER);
            xebble::SpriteInstance inst{
                .pos_x = static_cast<float>(px * TILE_SIZE),
                .pos_y = static_cast<float>(py * TILE_SIZE),
                .uv_x = uv.x, .uv_y = uv.y,
                .uv_w = uv.w, .uv_h = uv.h,
                .quad_w = static_cast<float>(TILE_SIZE),
                .quad_h = static_cast<float>(TILE_SIZE),
                .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            };
            renderer.submit_instances({&inst, 1}, sheet_->texture(), 3.0f);
        }

        // --- Draw HUD background ---
        {
            std::vector<xebble::SpriteInstance> hud_bg;
            auto wall_uv = sheet_->region(tiles::PERMANENT_WALL);
            for (uint32_t x = 0; x < VIEW_TILES_X; x++) {
                for (uint32_t row = 0; row < HUD_HEIGHT; row++) {
                    hud_bg.push_back({
                        .pos_x = static_cast<float>(x * TILE_SIZE),
                        .pos_y = static_cast<float>(MAP_VIEW_TILES_Y * TILE_SIZE + row * TILE_SIZE),
                        .uv_x = wall_uv.x, .uv_y = wall_uv.y,
                        .uv_w = wall_uv.w, .uv_h = wall_uv.h,
                        .quad_w = static_cast<float>(TILE_SIZE),
                        .quad_h = static_cast<float>(TILE_SIZE),
                        .r = 0.3f, .g = 0.3f, .b = 0.3f, .a = 1.0f,
                    });
                }
            }
            renderer.submit_instances(hud_bg, sheet_->texture(), 4.0f);
        }

        // --- Draw HUD text ---
        {
            float hud_y = static_cast<float>(MAP_VIEW_TILES_Y * TILE_SIZE);
            float gw = static_cast<float>(font_->glyph_width());
            float gh = static_cast<float>(font_->glyph_height());

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
                        .uv_x = uv.x, .uv_y = uv.y,
                        .uv_w = uv.w, .uv_h = uv.h,
                        .quad_w = gw, .quad_h = gh,
                        .r = r, .g = g, .b = b, .a = 1.0f,
                    });
                }
                if (!glyphs.empty()) {
                    renderer.submit_instances(glyphs, font_->sheet().texture(), 5.0f);
                }
            };

            // Status line
            auto status = std::format("Pos: ({},{})  Items: {}  [R]egenerate  [Esc]ape",
                                       player_x_, player_y_, items_collected_);
            draw_text(status, 4.0f, hud_y + 4.0f, 1.0f, 1.0f, 0.6f);

            // Message line
            if (!message_.empty()) {
                draw_text(message_, 4.0f, hud_y + 4.0f + gh + 2.0f, 0.8f, 0.8f, 0.8f);
            }
        }
    }

    void layout(uint32_t /*w*/, uint32_t /*h*/) override {}
};

int main() {
    auto assets_dir = find_assets_dir();
    std::filesystem::create_directories(assets_dir);

    // Generate bitmap font atlas if needed
    font_gen::generate_font(assets_dir / "font.png");

    // Generate manifest if not present (for non-bundle builds)
    auto manifest_path = assets_dir / "manifest.toml";
    if (!std::filesystem::exists(manifest_path)) {
        std::FILE* f = std::fopen(manifest_path.string().c_str(), "w");
        if (f) {
            std::fputs(
                "[spritesheets.tiles]\n"
                "path = \"angband-16x16.png\"\n"
                "tile_width = 16\n"
                "tile_height = 16\n"
                "\n"
                "[bitmap_fonts.font]\n"
                "path = \"font.png\"\n"
                "glyph_width = 8\n"
                "glyph_height = 8\n"
                "charset_start = 32\n"
                "charset_end = 126\n",
                f);
            std::fclose(f);
        }
    }

    return xebble::run(std::make_unique<RoguelikeDemo>(), {
        .window = {.title = "Xebble - Roguelike Demo", .width = 1280, .height = 720},
        .renderer = {.virtual_width = VIRTUAL_W, .virtual_height = VIRTUAL_H},
        .assets = {.directory = assets_dir, .manifest = manifest_path},
    });
}
```

**Step 2: Update `examples/basic_tilemap/assets/manifest.toml`**

```toml
[spritesheets.tiles]
path = "angband-16x16.png"
tile_width = 16
tile_height = 16

[bitmap_fonts.font]
path = "font.png"
glyph_width = 8
glyph_height = 8
charset_start = 32
charset_end = 126
```

Note: The `charset_start`/`charset_end` fields may need to be adapted to how the `AssetManager` currently loads bitmap fonts. Check `src/asset_manager.cpp` to see how the charset is handled. If it expects a literal charset string, generate it inline instead. The `font_gen::font_charset()` function produces the correct string for this range.

**Step 3: Build**

```bash
cmake --build build/debug 2>&1 | tail -20
```

Fix any compilation errors. Common issues:
- The manifest might need a `charset` field (string) instead of `charset_start`/`charset_end` — check `src/asset_manager.cpp` to see what it expects and adapt.
- `std::format` requires C++20/23 — should be available with our standard.
- The `font_gen.hpp` includes `stb_image_write.h` but `STB_IMAGE_WRITE_IMPLEMENTATION` is already defined in main.cpp — make sure it's only defined once.

**Step 4: Run**

```bash
./build/debug/examples/basic_tilemap
```

Expected: A window opens showing a dungeon rendered with the Angband tileset. Player moves with WASD/arrows. Monsters and items are scattered in rooms. HUD at the bottom shows position, item count, and contextual messages.

**Step 5: Commit**

```bash
git add examples/basic_tilemap/
git commit -m "feat(example): roguelike dungeon demo with Angband tileset, scrolling camera, HUD"
```

---

## Task 6: Verify and Polish

**Step 1: Check the tileset renders correctly**

Run the example and verify:
- [ ] Floor tiles show the correct Angband floor graphic
- [ ] Walls look like granite walls
- [ ] Doors appear at corridor/room junctions
- [ ] Stairs are visible in first and last rooms
- [ ] Player character renders as the Angband @ character
- [ ] Monsters render as recognizable creatures
- [ ] Items render correctly (gold piles, potions, etc.)
- [ ] HUD text is readable at the bottom

If any tiles look wrong (blank, wrong graphic), adjust the constants in `tiles.hpp`. The most likely issue is if the tileset has a different number of columns than 64 — verify by examining the image dimensions.

**Step 2: Test gameplay**

- [ ] WASD and arrow keys move the player one tile at a time
- [ ] Player cannot walk through walls
- [ ] Player cannot walk through monsters (message appears)
- [ ] Walking over items collects them and updates the count
- [ ] Camera scrolls to follow the player
- [ ] Camera clamps at map edges
- [ ] Pressing R regenerates a new dungeon
- [ ] Pressing Escape exits
- [ ] HUD shows correct position and messages

**Step 3: Fix any issues found**

Address rendering glitches, incorrect tile mappings, camera jitter, etc.

**Step 4: Build release and verify**

```bash
cmake --preset release && cmake --build build/release
./build/release/examples/basic_tilemap
```

**Step 5: Commit any fixes**

```bash
git add -A
git commit -m "fix(example): polish roguelike demo after testing"
```

---

## Task 7: Clean Up Old Generated Assets

**Step 1: Remove the old tile generation code**

The old `generate_test_tiles()` function and `stb_image_write` usage for tiles is no longer needed since we use the Angband tileset. However, we still need `stb_image_write` for the font generation. Keep the include and `STB_IMAGE_WRITE_IMPLEMENTATION` in main.cpp for that purpose.

Remove the old `tiles.png` from the assets directory if it still exists:
```bash
rm -f examples/basic_tilemap/assets/tiles.png
```

**Step 2: Update `.gitignore` in assets directory**

Make sure `font.png` is in `.gitignore` (it's generated at runtime) but `angband-16x16.png` is NOT (it's a checked-in asset):

```
font.png
```

**Step 3: Commit**

```bash
git add -A
git commit -m "chore(example): clean up old generated tile assets"
```
