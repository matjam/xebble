#pragma once

#include "tiles.hpp"
#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

struct Room {
    int x, y, w, h;

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
    bool is_monster;
    bool alive = true;
};

struct Dungeon {
    int width = 80;
    int height = 50;
    std::vector<uint32_t> floor_tiles;
    std::vector<uint32_t> feature_tiles;
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

    Entity* monster_at(int x, int y) {
        for (auto& e : entities)
            if (e.alive && e.is_monster && e.x == x && e.y == y) return &e;
        return nullptr;
    }

    Entity* item_at(int x, int y) {
        for (auto& e : entities)
            if (e.alive && !e.is_monster && e.x == x && e.y == y) return &e;
        return nullptr;
    }
};

inline Dungeon generate_dungeon(uint32_t seed = 0) {
    Dungeon dg;
    dg.floor_tiles.resize(dg.width * dg.height, tiles::GRANITE_WALL);
    dg.feature_tiles.resize(dg.width * dg.height, UINT32_MAX);

    std::mt19937 rng(seed ? seed : std::random_device{}());

    constexpr int MAX_ROOMS = 12;
    constexpr int MIN_ROOM_SIZE = 4;
    constexpr int MAX_ROOM_SIZE = 10;
    constexpr int MAX_ATTEMPTS = 200;

    int attempts = 0;
    while (static_cast<int>(dg.rooms.size()) < MAX_ROOMS && attempts < MAX_ATTEMPTS) {
        attempts++;
        int w = std::uniform_int_distribution<int>(MIN_ROOM_SIZE, MAX_ROOM_SIZE)(rng);
        int h = std::uniform_int_distribution<int>(MIN_ROOM_SIZE, MAX_ROOM_SIZE)(rng);
        int x = std::uniform_int_distribution<int>(1, dg.width - w - 1)(rng);
        int y = std::uniform_int_distribution<int>(1, dg.height - h - 1)(rng);

        Room room{x, y, w, h};
        bool overlap = false;
        for (auto& existing : dg.rooms) {
            if (room.intersects(existing, 2)) { overlap = true; break; }
        }
        if (overlap) continue;

        for (int ry = room.y; ry < room.y + room.h; ry++)
            for (int rx = room.x; rx < room.x + room.w; rx++)
                dg.floor_tiles[ry * dg.width + rx] = tiles::FLOOR;

        dg.rooms.push_back(room);
    }

    // Connect rooms with L-shaped corridors
    auto carve_h = [&](int x1, int x2, int y) {
        for (int x = std::min(x1, x2); x <= std::max(x1, x2); x++)
            if (dg.floor_tiles[y * dg.width + x] == tiles::GRANITE_WALL)
                dg.floor_tiles[y * dg.width + x] = tiles::FLOOR;
    };
    auto carve_v = [&](int y1, int y2, int x) {
        for (int y = std::min(y1, y2); y <= std::max(y1, y2); y++)
            if (dg.floor_tiles[y * dg.width + x] == tiles::GRANITE_WALL)
                dg.floor_tiles[y * dg.width + x] = tiles::FLOOR;
    };

    for (size_t i = 1; i < dg.rooms.size(); i++) {
        int cx1 = dg.rooms[i - 1].center_x(), cy1 = dg.rooms[i - 1].center_y();
        int cx2 = dg.rooms[i].center_x(), cy2 = dg.rooms[i].center_y();

        if (std::uniform_int_distribution<int>(0, 1)(rng)) {
            carve_h(cx1, cx2, cy1);
            carve_v(cy1, cy2, cx2);
        } else {
            carve_v(cy1, cy2, cx1);
            carve_h(cx1, cx2, cy2);
        }
    }

    // Place doors at corridor/room junctions
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
                if (std::uniform_int_distribution<int>(0, 9)(rng) < 3)
                    dg.feature_tiles[y * dg.width + x] = tiles::OPEN_DOOR;
            }
        }
    }

    // Stairs in first and last rooms
    if (dg.rooms.size() >= 2) {
        auto& first = dg.rooms.front();
        auto& last = dg.rooms.back();
        dg.feature_tiles[first.center_y() * dg.width + first.center_x()] = tiles::STAIRS_UP;
        dg.feature_tiles[last.center_y() * dg.width + last.center_x()] = tiles::STAIRS_DOWN;
    }

    // Player start
    if (!dg.rooms.empty()) {
        auto& start = dg.rooms.front();
        dg.player_start_x = start.x + 1;
        dg.player_start_y = start.y + 1;
    }

    // Monsters (1-2 per room, skip first)
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

    // Items (1-2 per room)
    for (auto& room : dg.rooms) {
        int num = std::uniform_int_distribution<int>(1, 2)(rng);
        for (int j = 0; j < num; j++) {
            int ex = std::uniform_int_distribution<int>(room.x + 1, room.x + room.w - 2)(rng);
            int ey = std::uniform_int_distribution<int>(room.y + 1, room.y + room.h - 2)(rng);
            if (ex == dg.player_start_x && ey == dg.player_start_y) continue;
            int ii = std::uniform_int_distribution<int>(0, tiles::NUM_ITEMS - 1)(rng);
            dg.entities.push_back({ex, ey, tiles::ITEMS[ii], tiles::ITEM_NAMES[ii], false});
        }
    }

    // Scatter rubble
    for (int y = 1; y < dg.height - 1; y++)
        for (int x = 1; x < dg.width - 1; x++) {
            if (dg.floor_tiles[y * dg.width + x] != tiles::FLOOR) continue;
            if (dg.feature_tiles[y * dg.width + x] != UINT32_MAX) continue;
            if (std::uniform_int_distribution<int>(0, 99)(rng) < 2)
                dg.feature_tiles[y * dg.width + x] = tiles::RUBBLE;
        }

    return dg;
}
