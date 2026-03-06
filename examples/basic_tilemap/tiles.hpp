#pragma once

#include <cstdint>

/// Tile indices into the Adam Bolt 16x16 Angband tileset (32 columns wide).
/// Derived from angband/lib/tiles/adam-bolt/graf-new.prf and flvr-new.prf.
/// Format: row = (attr - 0x80), col = (char - 0x80), index = row * 32 + col.
namespace tiles {

constexpr uint32_t COLS = 32;

constexpr uint32_t idx(uint8_t attr, uint8_t chr) {
    return static_cast<uint32_t>(attr - 0x80) * COLS + (chr - 0x80);
}

// --- Terrain ---
constexpr uint32_t FLOOR           = idx(0x80, 0x83);
constexpr uint32_t GRANITE_WALL    = idx(0x80, 0x86);
constexpr uint32_t PERMANENT_WALL  = idx(0x80, 0x95);
constexpr uint32_t OPEN_DOOR       = idx(0x82, 0x84);
constexpr uint32_t CLOSED_DOOR     = idx(0x82, 0x83);
constexpr uint32_t STAIRS_UP       = idx(0x80, 0x98);
constexpr uint32_t STAIRS_DOWN     = idx(0x80, 0x9B);
constexpr uint32_t RUBBLE          = idx(0x80, 0x9E);

// --- Player ---
constexpr uint32_t PLAYER          = idx(0x92, 0x80);

// --- Monsters ---
constexpr uint32_t SMALL_KOBOLD    = idx(0xA8, 0x99);
constexpr uint32_t KOBOLD          = idx(0xA8, 0x9A);
constexpr uint32_t LARGE_KOBOLD    = idx(0xA8, 0x9B);
constexpr uint32_t CAVE_ORC        = idx(0xA9, 0x8F);
constexpr uint32_t SNAGA           = idx(0xA9, 0x8E);
constexpr uint32_t LARGE_SNAKE     = idx(0xA2, 0x85);
constexpr uint32_t FRUIT_BAT       = idx(0xA5, 0x8F);
constexpr uint32_t GIANT_RAT       = idx(0xAC, 0x87);
constexpr uint32_t WOLF            = idx(0x9D, 0x9E);
constexpr uint32_t FOREST_TROLL    = idx(0xA3, 0x89);
constexpr uint32_t WHITE_JELLY     = idx(0xA8, 0x8A);
constexpr uint32_t FLOATING_EYE    = idx(0xA6, 0x9B);

// --- Items ---
constexpr uint32_t GOLD            = idx(0x83, 0x93);
constexpr uint32_t SCROLL          = idx(0x83, 0x9D);
constexpr uint32_t POTION_RED      = idx(0x85, 0x83);
constexpr uint32_t POTION_BLUE     = idx(0x85, 0x85);
constexpr uint32_t POTION_GREEN    = idx(0x85, 0x84);
constexpr uint32_t FOOD            = idx(0x8E, 0x84);
constexpr uint32_t TORCH           = idx(0x8E, 0x8B);
constexpr uint32_t DAGGER          = idx(0x8A, 0x8F);

// --- Rosters for random placement ---
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
